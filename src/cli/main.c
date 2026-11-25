#include "plike/cli.h"

int main(int argc, char **argv) {
    Options options;
    int status;
    if (!parse_options(argc, argv, &options, &status)) return status;
    return run_command(&options);
}
