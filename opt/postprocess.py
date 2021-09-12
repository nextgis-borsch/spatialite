from os.path import dirname, abspath
import subprocess

subprocess.Popen(['git', 'apply', './opt/spatialite_dem.patch'], cwd = dirname(dirname(abspath(__file__))))
subprocess.Popen(['git', 'apply', './opt/spatialite_xml_load.patch'], cwd = dirname(dirname(abspath(__file__))))
subprocess.Popen(['git', 'apply', './opt/spatialite_xml_print.patch'], cwd = dirname(dirname(abspath(__file__))))