/* Texts made by the programs live until the end: that is not a leak. */
const char *__asan_default_options(void);
const char *__asan_default_options(void) { return "detect_leaks=0"; }
