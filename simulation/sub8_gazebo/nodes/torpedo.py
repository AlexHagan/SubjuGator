#!/usr/bin/env python
import rospy
import rospkg
import tf

from gazebo_msgs.msg import ContactsState, ModelStates, ModelState
from gazebo_msgs.srv import SetModelState, GetModelState
from geometry_msgs.msg import Pose, Twist
from sub8_actuator_driver.srv import SetValve

from sub8_ros_tools import msg_helpers

import os
import numpy as np

# Replace this with launch file parameter.
rospack = rospkg.RosPack()
torpedo_path = os.path.join(rospack.get_path('sub8_gazebo'), 'models/torpedo/torpedo.sdf')


class TorpedoLauncher():
    def __init__(self):
        f = open(torpedo_path, 'r')
        self.sdf_f = f.read()

        self.launched = False

        rospy.Service('/actuator_driver/actuate', SetValve, self.launch_torpedo)
        self.set_torpedo = rospy.ServiceProxy('/gazebo/set_model_state', SetModelState)
        self.get_model = rospy.ServiceProxy('/gazebo/get_model_state', GetModelState)
        rospy.Subscriber('/contact_bumper/', ContactsState, self.check_contact, queue_size=1)

    def check_contact(self, msg):
        if not self.launched:
            return

        if len(msg.states) == 0:
            # If there is no impact don't worry about it.
            return

        torpedo_name = "torpedo::body::bodycol"
        board_name = "torpedo_board::hole_1::hole_1_col"

        for state in msg.states:
            if state.collision1_name == torpedo_name:
                break

        # Generally, if the torpedo is laying on the ground the collision force will be very small.
        # So if the force is really small, we assume the impact collision has occured before the torpedo was launched.
        print np.abs(np.linalg.norm(msg_helpers.rosmsg_to_numpy(state.total_wrench.force)))
        if np.abs(np.linalg.norm(msg_helpers.rosmsg_to_numpy(state.total_wrench.force))) < 10:
            rospy.loginfo("Torpedo probably still on the ground, still waiting.")
            return

        # Now the torpedo has impacted something, we gotta check what.
        rospy.loginfo('Impact detected!')
        self.launched = False

        if state.collision2_name == board_name:
            rospy.loginfo('Correct!')
        else:
            rospy.logwarn('Wrong!')

    def launch_torpedo(self, srv):
        '''
        Find position of sub and launch the torpedo from there.

        TODO:
            - Test to make sure it always fires from the right spot in the right direction.
                (It seems to but I haven't tested from all rotations.)
        '''
        sub_state = self.get_model(model_name='sub8')
        sub_pose = msg_helpers.pose_to_numpy(sub_state.pose)

        # Translate torpedo init velocity so that it first out of the front of the sub.
        muzzle_vel = np.array([10, 0, 0])
        v = rotate_vect_by_quat(np.append(muzzle_vel, 0), sub_pose[1])

        launch_twist = Twist()
        launch_twist.linear.x = v[0]
        launch_twist.linear.y = v[1]
        launch_twist.linear.z = v[2]

        # This is offset so it fires approx at the torpedo launcher location.
        launch_pos = rotate_vect_by_quat(np.array([.4, -.15, -.3, 0]), sub_pose[1])

        model_state = ModelState()
        model_state.model_name = 'torpedo'
        model_state.pose = msg_helpers.numpy_quat_pair_to_pose(sub_pose[1], sub_pose[0] + launch_pos)
        model_state.twist = launch_twist
        self.set_torpedo(model_state)
        self.launched = True


def rotate_vect_by_quat(v, q):
    '''
    Rotate a vector by a quaterion.
    v' = q*vq
    '''
    cq = np.array([-q[0], -q[1], -q[2], q[3]])
    cq_v = tf.transformations.quaternion_multiply(cq, v)
    v = tf.transformations.quaternion_multiply(cq_v, q)
    v[1:] *= -1
    return np.array(v)[:3]

if __name__ == '__main__':
    rospy.init_node('torpedo_manager')
    t = TorpedoLauncher()
    rospy.spin()