#include "pseudoscope/cli.h"
#include "pseudoscope/i18n.h"

int main(int argc, char **argv) {
    Options options;
    int status;
    locale_from_environment(); /* --lang, read with the other options, has the last word */
    if (!parse_options(argc, argv, &options, &status)) return status;
    return run_command(&options);
}
