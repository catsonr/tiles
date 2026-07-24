#include "TestHarness.h"

namespace tiles_test {

std::vector<Registry::Case> &Registry::cases() {
    static std::vector<Case> instance;
    return instance;
}

int &Registry::checks() {
    static int instance = 0;
    return instance;
}

int &Registry::failures() {
    static int instance = 0;
    return instance;
}

Registrar::Registrar(const char *p_name, std::function<void()> p_fn) {
    Registry::cases().push_back({ p_name, std::move(p_fn) });
}

} // namespace tiles_test

int main() {
    using namespace tiles_test;

    for (const auto &test_case : Registry::cases()) {
        const int before = Registry::failures();
        test_case.fn();
        const int after = Registry::failures();
        std::printf("[%s] %s\n", after == before ? "pass" : "FAIL", test_case.name.c_str());
    }

    std::printf(
        "\n%d checks, %d failed across %zu cases\n",
        Registry::checks(),
        Registry::failures(),
        Registry::cases().size());

    return Registry::failures() == 0 ? 0 : 1;
}
