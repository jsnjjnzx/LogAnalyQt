import re
import unittest
from pathlib import Path


MAIN_QML = Path(__file__).resolve().parents[1] / "qml" / "Main.qml"


class MainWindowMinimumWidthTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = MAIN_QML.read_text(encoding="utf-8")

    def test_minimum_width_includes_every_horizontal_margin(self):
        required_terms = (
            "mainContentLayout.anchors.leftMargin",
            "mainContentLayout.anchors.rightMargin",
            "folderGroupBox.leftPadding",
            "folderGroupBox.rightPadding",
            "folderLayout.anchors.leftMargin",
            "folderLayout.anchors.rightMargin",
        )
        for term in required_terms:
            self.assertIn(term, self.source)

        self.assertRegex(
            self.source,
            re.compile(
                r"minimumWidth:\s*Math\.max\(800,\s*Math\.ceil\("
                r"folderLayout\.implicitWidth\s*\+\s*toolbarHorizontalChrome\)\)"
            ),
        )

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
