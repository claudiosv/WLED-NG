/* DateStrings.cpp
 * Definitions for date strings for use with the Time library
 *
 * Updated for Arduino 1.5.7 18 July 2014
 *
 * No memory is consumed in the sketch if your code does not call any of the string methods
 * You can change the text of the strings, make sure the short strings are each exactly 3 characters
 * the long strings can be any length up to the constant dt_MAX_STRING_LEN defined in TimeLib.h
 *
 */

// #if defined(__AVR__)
// #include <avr/pgmspace.h>
// #else
// // for compatibility with Arduino Due and Teensy 3.0 and maybe others?
// #define PROGMEM
#define PGM_P               const char*
#define pgm_read_byte(addr) (*(const unsigned char*)(addr))
#define pgm_read_word(addr) (*(const unsigned char**)(addr))
// #define strcpy_P(dest, src) strcpy((dest), (src))
// #endif
#include <string.h>  // for strcpy or strcpy

#include "TimeLib.h"

// the short strings for each day or month must be exactly dt_SHORT_STR_LEN
#define dt_SHORT_STR_LEN 3  // the length of short strings

static char buffer[dt_MAX_STRING_LEN + 1];  // must be big enough for longest string and the terminating null

// const char monthStr0[] = "";
// const char monthStr1[] = "January";
// const char monthStr2[] = "February";
// const char monthStr3[] = "March";
// const char monthStr4[] = "April";
// const char monthStr5[] = "May";
// const char monthStr6[] = "June";
// const char monthStr7[] = "July";
// const char monthStr8[] = "August";
// const char monthStr9[] = "September";
// const char monthStr10[] = "October";
// const char monthStr11[] = "November";
// const char monthStr12[] = "December";

// const char * const monthNames_P[] =
// {
//     monthStr0,monthStr1,monthStr2,monthStr3,monthStr4,monthStr5,monthStr6,
//     monthStr7,monthStr8,monthStr9,monthStr10,monthStr11,monthStr12
// };
inline constexpr const char* const monthNames[] = {"",        "January",  "February", "March",  "April",
                                                   "May",     "June",     "July",     "August", "September",
                                                   "October", "November", "December"};

// const char monthShortNames_P[] = "ErrJanFebMarAprMayJunJulAugSepOctNovDec";
inline constexpr char monthShortNames[] = "ErrJanFebMarAprMayJunJulAugSepOctNovDec";

// const char dayStr0[] = "Err";
// const char dayStr1[] = "Sunday";
// const char dayStr2[] = "Monday";
// const char dayStr3[] = "Tuesday";
// const char dayStr4[] = "Wednesday";
// const char dayStr5[] = "Thursday";
// const char dayStr6[] = "Friday";
// const char dayStr7[] = "Saturday";

// const char * const dayNames_P[] =
// {
//    dayStr0,dayStr1,dayStr2,dayStr3,dayStr4,dayStr5,dayStr6,dayStr7
// };

inline constexpr const char* const dayNames[] = {"Err",       "Sunday",   "Monday", "Tuesday",
                                                 "Wednesday", "Thursday", "Friday", "Saturday"};

// const char dayShortNames_P[] = "ErrSunMonTueWedThuFriSat";
inline constexpr char dayShortNames[] = "ErrSunMonTueWedThuFriSat";

/* functions to return date strings */

char* monthStr(uint8_t month) {
  strcpy(buffer, (PGM_P)pgm_read_word(&(monthNames[month])));
  return buffer;
}

char* monthShortStr(uint8_t month) {
  for (int i = 0; i < dt_SHORT_STR_LEN; i++) {
    buffer[i] = pgm_read_byte(&(monthShortNames[i + (month * dt_SHORT_STR_LEN)]));
  }
  buffer[dt_SHORT_STR_LEN] = 0;
  return buffer;
}

char* dayStr(uint8_t day) {
  strcpy(buffer, (PGM_P)pgm_read_word(&(dayNames[day])));
  return buffer;
}

char* dayShortStr(uint8_t day) {
  uint8_t index = day * dt_SHORT_STR_LEN;
  for (int i = 0; i < dt_SHORT_STR_LEN; i++) {
    buffer[i] = pgm_read_byte(&(dayShortNames[index + i]));
  }
  buffer[dt_SHORT_STR_LEN] = 0;
  return buffer;
}
