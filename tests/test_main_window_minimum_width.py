import re
import unittest
from pathlib import Path


MAIN_QML = Path(__file__).resolve().parents[1] / "qml" / "Next" / "Main.qml"


class MainWindowMinimumWidthTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = MAIN_QML.read_text(encoding="utf-8")

    def test_workspace_has_minimum_size(self):
        self.assertRegex(self.source, r"minimumWidth:\s*1200\b")
        self.assertRegex(self.source, r"minimumHeight:\s*740\b")

    def test_saved_width_is_clamped_to_the_current_minimum(self):
        self.assertRegex(
            self.source,
            re.compile(
                r"var savedWidth\s*=.*?;\s*"
                r"mainWindow\.width\s*=\s*Math\.max\(savedWidth,\s*"
                r"mainWindow\.minimumWidth\);",
                re.DOTALL,
            ),
        )
        self.assertIn("onMinimumWidthChanged: ensureMinimumWindowWidth()", self.source)


if __name__ == "__main__":
    unittest.main()
