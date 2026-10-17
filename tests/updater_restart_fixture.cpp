#include <cstdlib>
#include <fstream>

int main() {
  const char *marker = std::getenv("LOGANALYQT_RESTART_MARKER");
  if (!marker) return 1;
  std::ofstream output(marker);
  output << "restarted " << APP_VERSION;
  return output ? 0 : 1;
}
