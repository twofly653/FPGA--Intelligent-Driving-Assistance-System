"""Explicit offline gate: disable socket creation for this complete regression."""
import json,unittest
from unittest.mock import patch
from pathlib import Path

if __name__=='__main__':
    suite=unittest.defaultTestLoader.loadTestsFromNames([
        'test_net_protocol','test_live_tracking','test_tracking_lifecycle',
        'test_precision_quality','test_latest_artifacts'])
    with patch('socket.socket',side_effect=AssertionError('No network sockets in offline tests')):
        result=unittest.TextTestRunner(verbosity=2).run(suite)
    Path('validation/r16_offline_checks.json').write_text(json.dumps({
        'tests':result.testsRun,'failures':len(result.failures),'errors':len(result.errors),
        'sockets_forbidden':True,'hardware_access':False},indent=2),encoding='utf-8')
    raise SystemExit(0 if result.wasSuccessful() else 1)
