#include <cstdio>
#include "dino8/kernel/brep.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  Brep box = Brep::Box(0, 0, 0, 1, 1, 1);
  std::printf("edge count = %d\n", box.raw().m_E.Count());

  try {
    box.RefitTrim(box.raw().m_E.Count() + 50, 0.01);
    std::printf("ERROR: expected throw\n");
  } catch (const std::out_of_range& e) {
    std::printf("OK out_of_range: %s\n", e.what());
  }

  std::printf("about to mark edge 0 deleted\n");
  box.raw().m_E[0].m_edge_index = -1;
  std::printf("marked. about to call RefitTrim(0, ...)\n");
  try {
    box.RefitTrim(0, 0.01);
    std::printf("ERROR: expected throw\n");
  } catch (const std::invalid_argument& e) {
    std::printf("OK invalid_argument: %s\n", e.what());
  }
  std::printf("done\n");

  ON::End();
  return 0;
}
