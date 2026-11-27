// c5r::fow header helpers (lead-0168, 2026-11-14): stateFromFowBits and fowAnim against fog-of-war.md 2.5, 3.2, 3.6.
#include <c5r/fow/fow.h>
#include <cmath>
#include <cstdio>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

int main()
{
    using namespace c5r::fow;
    CHECK(stateFromFowBits(3) == PlotState::Visible);
    CHECK(stateFromFowBits(2) == PlotState::Visible);
    CHECK(stateFromFowBits(1) == PlotState::Seen);
    CHECK(stateFromFowBits(0) == PlotState::Unexplored);
    CHECK(stateFromFowBits(0xF1) == PlotState::Seen);   // high bits ignored here; the scene counts them

    float a[4];
    fowAnim(3583.0f, 10.0, a);                          // the Tiny map of 3951: k = 7 / 3583 = 1 / 511.857
    CHECK(std::fabs(a[0] - 0.00195367f) < 1e-7f);
    CHECK(a[0] == a[1]);
    CHECK(std::fabs(a[2] + 20.0f) < 1e-4f);
    CHECK(a[3] == 0.0f);
    fowAnim(0.0f, 5.0, a);                              // before a game: stock's 0.002 everywhere
    CHECK(a[0] == 0.002f && a[1] == 0.002f && a[2] == 0.002f && a[3] == 0.002f);
    fowAnim(-1.0f, 5.0, a);
    CHECK(a[0] == 0.002f);
    fowAnim(std::nanf(""), 5.0, a);                     // NaN width: the pre-game value, never NaN out
    CHECK(a[0] == 0.002f && !std::isnan(a[2]));

    std::printf("%s fowanim (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
