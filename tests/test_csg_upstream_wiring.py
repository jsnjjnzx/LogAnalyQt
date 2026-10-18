import unittest
from pathlib import Path


class CsgUpstreamWiringTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parents[1]
        cls.cmake = (cls.root / "CMakeLists.txt").read_text(
            encoding="utf-8-sig"
        )
        cls.backend = (
            cls.root / "src/core/LogBackend.cpp"
        ).read_text(encoding="utf-8")
        cls.main_qml = (cls.root / "qml/Next/Main.qml").read_text(
            encoding="utf-8"
        )
        cls.sync_cpp = (
            cls.root / "src/core/ProtocolSyncManager.cpp"
        ).read_text(encoding="utf-8")
        cls.sync_qml = (
            cls.root / "qml/Next/ProtocolSyncDialog.qml"
        ).read_text(encoding="utf-8")
        cls.detector_header = (
            cls.root / "src/core/ProtocolDetector.h"
        ).read_text(encoding="utf-8")
        cls.csg_xml = (
            cls.root / "protocol_xml/csg_upstream_protocol.xml"
        ).read_text(encoding="utf-8")

    def test_cmake_packages_sources_and_xml(self):
        self.assertIn(
            "csg_upstream/CsgUpstreamConfigManager.cpp", self.cmake
        )
        self.assertIn(
            "csg_upstream/CsgUpstreamFieldParser.cpp", self.cmake
        )
        self.assertIn(
            "csg_upstream/CsgUpstreamValueDecoder.cpp", self.cmake
        )
        self.assertIn(
            "csg_upstream/CsgUpstreamServiceParser.cpp", self.cmake
        )
        self.assertIn("csg_upstream/CsgUpstreamParser.cpp", self.cmake)
        self.assertIn(
            "protocol_xml/csg_upstream_protocol.xml", self.cmake
        )

    def test_backend_extracts_loads_and_dispatches_protocol(self):
        self.assertGreaterEqual(
            self.backend.count('"csg_upstream_protocol.xml"'), 2
        )
        self.assertIn(
            'resolveProtocolPath("csg_upstream_protocol.xml")',
            self.backend,
        )
        self.assertIn(
            "CsgUpstreamConfigManager::instance().loadXml", self.backend
        )
        self.assertIn(
            "case ProtocolType::ProtocolCsgUpstream:", self.backend
        )
        self.assertIn("CsgUpstreamParser parser", self.backend)
        self.assertIn("CsgUpstreamParser::getSummary(data)", self.backend)
        self.assertIn(
            "CsgUpstreamConfigManager::instance().getVersion()",
            self.backend,
        )

    def test_all_manual_protocol_lists_include_display_name(self):
        self.assertEqual(self.main_qml.count('"南网计量上行规约"'), 3)

    def test_main_protocol_selector_has_room_for_display_name(self):
        self.assertIn("Layout.preferredWidth: 180", self.main_qml)

    def test_smart_framing_scope_is_unchanged(self):
        smart_section = self.backend.split(
            "QVariantList LogBackend::listSmartFramingProtocols()", 1
        )[1].split(
            "QVariantList LogBackend::list698ServiceTypes()", 1
        )[0]
        self.assertNotIn("ProtocolCsgUpstream", smart_section)

    def test_protocol_enum_value_is_appended_after_645(self):
        self.assertRegex(
            self.detector_header,
            r"Protocol645\s*=\s*7[\s\S]*ProtocolCsgUpstream\s*=\s*8",
        )

    def test_xml_has_independent_afn_and_di_tables(self):
        self.assertEqual(self.csg_xml.count("<AFN "), 17)
        self.assertEqual(self.csg_xml.count("<DI "), 667)
        self.assertIn('code="E00003XX"', self.csg_xml)
        self.assertNotIn('code="E000033E"', self.csg_xml)
        self.assertIn('code="050601FF"', self.csg_xml)
        self.assertIn('code="050602FF"', self.csg_xml)
        self.assertNotIn('min="01"', self.csg_xml)
        self.assertNotIn('name="普通任务号{N}"', self.csg_xml)
        self.assertIn('type="DataItemGroups"', self.csg_xml)
        self.assertIn("<AFNs>", self.csg_xml)
        self.assertIn("<DIs>", self.csg_xml)
        self.assertEqual(self.csg_xml.count("<Service "), 33)
        self.assertEqual(self.csg_xml.count('type="DataUnits"'), 31)
        self.assertIn('direction="down" type="Struct"', self.csg_xml)
        self.assertIn('direction="up" type="Choice"', self.csg_xml)
        self.assertIn('layout="content" cardinality="many"', self.csg_xml)
        self.assertIn('layout="query" cardinality="many"', self.csg_xml)
        self.assertIn('pw="required" tp="seq"', self.csg_xml)
        self.assertIn('version="v2.0.0"', self.csg_xml)
        self.assertIn('display="ConfirmationResult"', self.csg_xml)
        self.assertIn('display="SecurityModuleFlag"', self.csg_xml)
        afn_section = self.csg_xml.split("<AFNs>", 1)[1].split(
            "</AFNs>", 1
        )[0]
        self.assertNotIn("<DI ", afn_section)

    def test_sync_validates_and_hot_reloads_csg_xml(self):
        self.assertIn('"csg_upstream_protocol.xml"', self.sync_cpp)
        self.assertIn('"ProtCsgUpstreamConfig"', self.sync_cpp)
        self.assertIn("validateProtocolSemantics", self.sync_cpp)
        self.assertIn("protocolFileDownloaded", self.backend)
        self.assertIn("热加载南网计量上行规约 XML 失败", self.backend)

    def test_sync_dialog_explains_remote_missing_file(self):
        self.assertIn("远端缺失，待上传", self.sync_qml)
        self.assertIn("syncStatusText", self.sync_qml)


if __name__ == "__main__":
    unittest.main()
