import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


EXPECTED_AFNS = {
    "00",
    "02",
    "04",
    "06",
    "0A",
    "0C",
    "0D",
    "0E",
    "0F",
    "10",
    "12",
    "13",
    "14",
    "15",
    "16",
    "17",
    "20",
}

class CsgUpstreamCoverageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parents[1]
        xml_root = ET.parse(
            cls.root / "protocol_xml/csg_upstream_protocol.xml"
        ).getroot()
        cls.xml_root = xml_root
        cls.configured_afns = {
            element.attrib["code"].upper()
            for element in xml_root.findall("./AFNs/AFN")
        }

    def test_final_xml_is_independent_and_consistent(self):
        self.assertEqual(self.xml_root.attrib.get("version"), "v2.0.0")
        self.assertFalse(self.xml_root.findall(".//Value"))

        afn_elements = self.xml_root.findall("./AFNs/AFN")
        di_elements = self.xml_root.findall("./DIs/DI")
        di_codes = [element.attrib["code"].upper() for element in di_elements]
        self.assertEqual(len(di_codes), len(set(di_codes)))
        self.assertEqual(len(di_codes), 667)

        association_attributes = {"ref", "binding", "schema", "afn"}
        for element in afn_elements + di_elements:
            self.assertFalse(
                association_attributes.intersection(
                    attribute.lower() for attribute in element.attrib
                ),
                element.attrib.get("code"),
            )
        for afn in afn_elements:
            self.assertFalse(afn.findall(".//DI"), afn.attrib["code"])

    def test_task_1_xml_configures_every_expected_afn(self):
        self.assertEqual(self.configured_afns, EXPECTED_AFNS)


if __name__ == "__main__":
    unittest.main()
