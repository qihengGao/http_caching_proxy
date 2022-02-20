#ifndef __MYTIME_HPP__
#define __MYTIME_HPP__
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <unordered_map>

class MyTime {
 public:
  time_t now;
  time_t gmttime;

  MyTime() {
    this->now = time(0);
    struct tm* gmttm = gmtime(&now);
    gmttime = mktime(gmttm);
  }
  MyTime(std::string printTime) {
    std::unordered_map<std::string, int> month{
        {"Jan", 0}, {"Feb", 1}, {"Mar", 2},  {"Apr", 3},
        {"May", 4}, {"Jun", 5}, {"Jul", 6},  {"Aug", 7},
        {"Sep", 8}, {"Oct", 9}, {"Nov", 10}, {"Dec", 11}};

    struct tm tm;
    int day_index = printTime.find(",");
    int day_end = printTime.find(" ", day_index + 2);
    tm.tm_mday =
        atoi(printTime.substr(day_index + 2, day_end - day_index - 2).c_str());
    int month_end = printTime.find(" ", day_end + 1);
    tm.tm_mon =
        month.find(printTime.substr(day_end + 1, month_end - day_end - 1))
            ->second;
    int year_end = printTime.find(" ", month_end + 1);
    tm.tm_year =
        atoi(
            printTime.substr(month_end + 1, year_end - month_end - 1).c_str()) -
        1900;
    int hour_end = printTime.find(":", year_end + 1);
    tm.tm_hour =
        atoi(printTime.substr(year_end + 1, hour_end - year_end - 1).c_str());
    int min_end = printTime.find(":", hour_end + 1);
    tm.tm_min =
        atoi(printTime.substr(hour_end + 1, min_end - hour_end - 1).c_str());
    int sec_end = printTime.find(" ", min_end + 1);
    tm.tm_sec =
        atoi(printTime.substr(min_end + 1, sec_end - min_end - 1).c_str());
    tm.tm_isdst = 0;
    now = mktime(&tm);
  }

  std::string getPrintTime() {
    char* dt = ctime(&now);
    std::string temp(dt);
    return temp.substr(0, temp.size() - 1);
  }

  int getDiff(const MyTime& time) const {
    return abs((int)(time.now - this->gmttime));
  }
};

#endif