#include "dsm.hpp"

int main() {
    DSM dsm("1"); // a UUID like string should be used;
    void* addr = dsm.dsm_malloc(4096);
    std::stringstream ss;
    ss << std::hex << addr;
    std::cout << ss.str() << std::endl;

    std::cout << *((char*)addr + 1) << std::endl;
    *((char*)addr + 1) = 1;

    while (true) {};
}