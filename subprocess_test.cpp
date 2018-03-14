#include "subprocess.h"

#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
    char c;
    std::string buf;
    Subprocess sub;

    std::cin >> std::noskipws;
    while (std::cin >> c) {
        buf += c;
    }

    sub.set_cmd(argv[1]);
    for (int i = 2; i < argc; ++i) {
        sub.add_arg(argv[i]);
    }
    sub.set_input(buf);

    const Subprocess &csub = sub;
    bool result = sub.run_and_wait();
    std::cout << "result: " << result << std::endl;
    std::cout << "out: <" << csub.output() << ">" << std::endl;
    std::cout << "err: <" << csub.error() << ">" << std::endl;
    std::cout << "stat: " << csub.stats() << std::endl;
}
