#pragma once
bool IntroLogInit();
void IntroLog(const char *format, ...) __attribute__((format(printf,1,2)));
void IntroLogFlush();
