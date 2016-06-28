#include "fetch_image.hpp"
#include <string>
#include "afilesystem.hpp"
#include <vector>
#include "astring_view.hpp"

void fetch_image::erase_directory(const astd::filesystem::path& path)
{
   if (astd::filesystem::exists(path))
   {
      astd::filesystem::error_code ec;
      auto it = astd::filesystem::directory_iterator(path);
      auto ite = astd::filesystem::directory_iterator();

      while (it != ite)
      {
         if (!astd::filesystem::remove(*it, ec))
         {
            std::cerr << "error removing " << it->path() << " : " << ec.message() << std::endl;
         }
         ++it;
      }
      if (!astd::filesystem::remove(path, ec))
         std::cerr << "error removing " << path << " : " << ec.message() << std::endl;
   }
}

bool fetch_image::is_404(const std::vector<char>& buffer)
{
   static std::vector<astd::string_view> file_not_found{ "404 Not Found", "Error 404 - Not found", "300 Multiple Choices", "301 Moved Permanently" };
   for (auto& error : file_not_found)
   {
      if (std::search(buffer.begin(), buffer.end(), error.begin(), error.end()) != buffer.end())
         return true;
   }
   return false;
}

//return: image_name, image_content
fetch_image::image fetch_image::fetch_chapter_image(std::string partial_url
   , const source_token& image_token
   , std::size_t image_index
   , astd::string_view directory_name)
{
   std::vector<char> file_buffer;
   file_buffer.reserve(FILE_BUFFER_SIZE);
   std::string full_url = source_token::remplace_token(partial_url, image_token, image_index);
   astd::string_view source_image_name = astd::string_view(full_url).substr(full_url.find_last_of('/') + 1);
   std::string image_name = (directory_name.to_string() + "_").append(source_image_name.data(), source_image_name.size());

   file_buffer.clear();

   auto getter = http::curl_get(full_url, [&file_buffer](astd::string_view buffer) mutable
   { file_buffer.insert(file_buffer.end(), buffer.begin(), buffer.end()); });

   if (!getter.first || is_404(file_buffer))
   {
      return{};
   }
   else
   {
      return{ image_name, file_buffer };
   }
}

int fetch_image::write_chapter(const fetch_image::chapter& chap)
{
   int result = CHAPTER_NOT_FOUND;
   if (chap.images.size())
   {
      result = NONE;

      if (!astd::filesystem::exists(chap.directory_full_path))
      {
         astd::filesystem::error_code ec;
         if (!astd::filesystem::create_directories(chap.directory_full_path, ec))
         {
            std::cerr << ec.message() << std::endl;
            result = CREATE_DESTINATION_FAILED;
         }
      }

      if (result == NONE)
      {
         for (auto& image : chap.images)
         {
            auto image_full_path = chap.directory_full_path / image.name;

            std::ofstream stream{ image_full_path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary };
            if (!stream)
            {
               result = CREATE_IMAGE_FILE_FAILED;
            }
            else
            {
               stream.write(image.content.data(), image.content.size());
            }
         }
      }

      if (result == CREATE_IMAGE_FILE_FAILED)
      {
         erase_directory(chap.directory_full_path);
      }
   }

   return result;
}

void fetch_image::update_perms(const astd::filesystem::path path)
{
#ifdef __linux
{
   std::stringstream command;
   command << "sudo chown -R btsync:syncapp " << path;
   int result = std::system(command.str().c_str());
   if (result != EXIT_SUCCESS)
      std::cout << "chown failed" << std::endl;
}
{
   std::stringstream command;
   command << "sudo chmod +w " << path;
   int result = std::system(command.str().c_str());
   if (result != EXIT_SUCCESS)
      std::cout << "chmod failed" << std::endl;
}
#endif
}

int fetch_image::fetch_chapter(anime_database& db, const source_token& number_token, const source_token& image_token, std::size_t source_index, std::size_t number_index)
{
   std::string directory_name = db.input.name + "_" + db.input.language + "_" + number_token.values[number_index];

   std::string partial_url = source_token::remplace_token(db.input.sources[source_index], number_token, number_index);

   std::size_t image_index = 0;
   std::size_t size = image_token.values.size();
   std::size_t batch_success_count = 1;

   chapter chap;
   chap.directory_full_path = db.input.destination / directory_name;

   while (image_index < size && batch_success_count > 0)
   {
      std::vector< std::future< image > > image_get;
      for (std::size_t i = 0; i < MAX_ERROR_ON_CHAPTER && image_index < size; ++i)
      {
         image_get.emplace_back(std::async(fetch_chapter_image, partial_url, image_token, image_index, directory_name));
         ++image_index;
      }

      batch_success_count = 0;
      for (auto& image_get_result : image_get)
      {
         auto image = image_get_result.get();
         if (image.name.size())
         {
            batch_success_count++;
            chap.images.emplace_back(image);
         }
      }
   }

   int result = write_chapter(chap);
   if (result == NONE)
   {
      std::cout << "push value " << number_token.values[number_index] << std::endl;
      db.number_fetched.push_back(number_token.values[number_index]);
      update_perms(chap.directory_full_path);
   }
   return result;
}

int fetch_image::fetch_from_source(anime_database& db, std::size_t source_index, const astd::string_view starting_number)
{
   source_token number_token(db.input.sources[source_index], "number", starting_number);
   if (!number_token.valid)
   {
      return SOURCE_PATH_PARSING_FAILED;
   }

   source_token image_token(db.input.sources[source_index], "image", "0");
   if (!image_token.valid)
   {
      return SOURCE_PATH_PARSING_FAILED;
   }

   int result = CHAPTER_NOT_FOUND;
   for (std::size_t number_index = 0; number_index < number_token.values.size() && result == CHAPTER_NOT_FOUND; ++number_index)
   {
      result = fetch_chapter(db, number_token, image_token, source_index, number_index);
   }
   return result;
}

int fetch_image::fetch_next_number(anime_database& db)
{
   std::string starting_number;
   if (db.number_fetched.size())
   {
      starting_number = db.number_fetched.back();
   }
   else
   {
      starting_number = db.input.starting_number;
   }

   int result = CHAPTER_NOT_FOUND;
   for (std::size_t source_index = 0; source_index < db.input.sources.size() && result == CHAPTER_NOT_FOUND; ++source_index)
   {
      result = fetch_from_source(db, source_index, starting_number);
   }
   return result;
}

std::pair<int, std::size_t> fetch_image::fetch(const configuration_input& input)
{
   int result = NONE;
   anime_database db;
   astd::filesystem::path db_path = input.destination / (input.name + ".db");

   auto path_string = astd::basic_string_view<astd::filesystem::path::value_type>(db_path.c_str());
   if (std::any_of(path_string.begin(), path_string.end(), [](auto val)
   { return val == '~'; }))
   {
      return{ DESTINATION_INVALID, 0 };
   }

   astd::filesystem::error_code ec;
   if (!astd::filesystem::exists(input.destination)
      && !astd::filesystem::create_directories(input.destination, ec))
   {
      std::cerr << "create target directory failed! (dir: " << input.destination << ")" << std::endl;
      std::cerr << ec.message() << std::endl;
      return{ CREATE_DESTINATION_FAILED, 0 };
   }

   if (astd::filesystem::exists(db_path))
   {
      auto load_db = anime_database::parse_anime_db(db_path);
      if (!load_db.first)
      {
         std::cerr << "load db failed: " << db_path << std::endl;
         return{ LOAD_DB_FAILED, 0 };
      }
      else
      {
         db = std::move(load_db.second);
      }
   }

   db.input = input;

   std::size_t num_fetched = 0;

   while ((result = fetch_next_number(db)) == NONE)
   {
      num_fetched++;
      anime_database::dump_anime_db(db_path, db);
   }

   return{ result, num_fetched };
}