#ifndef ANIME_DB_HPP
#define ANIME_DB_HPP

#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include "afilesystem.hpp"
#include "configuration_input.hpp"

struct anime_database
{
   configuration_input input;
   std::vector<std::string> number_fetched;

   static bool dump_anime_db(const astd::filesystem::path& db_path, const anime_database& db)
   {
      bool result = false;
      std::ofstream stream{ db_path.c_str(), std::ios::out | std::ios::trunc };

      if (stream)
      {
         result = true;
         configuration_input::dump_config(stream, db.input);
         for (auto& num : db.number_fetched)
         {
            stream << num << std::endl;
         }
      }
      return result;
   }

   static std::pair<bool, anime_database> parse_anime_db(const astd::filesystem::path& db_path)
   {
      std::pair<bool, anime_database> result{ false, anime_database() };

      std::ifstream stream{ db_path.c_str() };
      if (stream)
      {
         std::string line;
         result.first = true;
         bool ok = true;
         std::size_t line_num = 1;
         while (result.first && std::getline(stream, line))
         {
            ok = ok && configuration_input::parse_line(line, result.second.input);
            if (!ok)
            {
               if (std::all_of(line.begin(), line.end(), [](auto& c) { return std::isdigit(c) != 0 || c == '.'; })) //if the line is a interger/float
               {
                  result.second.number_fetched.emplace_back(line);
               }
               else //something when bad
               {
                  result.first = false;
               }
            }
            
            if (!result.first)
               std::cerr << "error at line : no " << line_num << " : " << line << std::endl;
            
            line_num++;
         }
      }
      return result;
   }
};



#endif //!ANIME_DB_HPP
