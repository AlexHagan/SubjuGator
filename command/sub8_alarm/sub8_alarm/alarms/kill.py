#!/usr/bin/env python
import rospy
from kill_handling.broadcaster import KillBroadcaster


class LegacyKill(object):
    alarm_name = 'legacy_kill'

    def __init__(self):
        # Keep some knowledge of which thrusters we have working
        self.dropped_thrusters = []
        self.kb = KillBroadcaster(id='network', description='Network timeout')

    def handle(self, time_sent, parameters):
        self.kb.send(active=True)

    def cancel(self, time_sent, parameters):
        self.kb.send(active=False)
