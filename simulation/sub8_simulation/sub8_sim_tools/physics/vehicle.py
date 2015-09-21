import numpy as np
import rospy
import tf
from time import time
from sub8_sim_tools.physics.physics import Box
from sub8_msgs.msg import Thrust, ThrusterCmd
import geometry_msgs.msg as geometry
from nav_msgs.msg import Odometry
from std_msgs.msg import Header


class Sub8(Box):
    _cmd_timeout = np.inf
    # _cmd_timeout = 10.
    def __init__(self, world, space, position):
        '''Yes, right now we're approximating the sub as a box
        Not taking any parameters because this is the literal Sub8
        TODO:
            - Make the thruster list a parameter

        See Annie for how the thrusters work
        '''
        lx, ly, lz = 0.5, 0.2, 0.2
        density = 5
        super(self.__class__, self).__init__(world, space, position, density, lx, ly, lz)

        self.truth_odom_pub = rospy.Publisher('truth/odom', Odometry, queue_size=1)
        self.thruster_sub = rospy.Subscriber('thrusters/thrust', Thrust, self.thrust_cb, queue_size=2)
        self.thrust_dict = {}
        # Make this a parameter

        # name, relative direction, relative position (COM)
        self.thruster_list = [
            ("FLV", np.array([ 0.000,  0.0, -1]), np.array([ 0.1583, 0.16900, 0.0142])),
            ("FLL", np.array([-0.866,  0.5,  0]), np.array([ 0.2678, 0.27950, 0.0000])),
            ("FRV", np.array([ 0.000,  0.0, -1]), np.array([ 0.1583, -0.1690, 0.0142])),
            ("FRL", np.array([-0.866, -0.5,  0]), np.array([ 0.2678, -0.2795, 0.0000])),
            ("BLV", np.array([ 0.000,  0.0,  1]), np.array([-0.1583, 0.16900, 0.0142])),
            ("BLL", np.array([ 0.866,  0.5,  0]), np.array([-0.2678, 0.27950, 0.0000])),
            ("BRV", np.array([ 0.000,  0.0,  1]), np.array([-0.1583, -0.1690, 0.0142])),
            ("BRL", np.array([ 0.866, -0.5,  0]), np.array([-0.2678, -0.2795, 0.0000])),
        ]
        self.last_cmd_time = time()

    def publish_pose(self):
        pose_matrix = np.transpose(self.pose)
        linear_vel, angular_vel = self.velocity
        quaternion = tf.transformations.quaternion_from_matrix(pose_matrix)
        translation = tf.transformations.translation_from_matrix(pose_matrix)

        header = Header(
            stamp=rospy.Time.now(),
            frame_id='/world'
        )
        pose = geometry.Pose(
            position=geometry.Point(*translation),
            orientation=geometry.Quaternion(*quaternion),
        )

        twist = geometry.Twist(
            linear=geometry.Vector3(*linear_vel),
            angular=geometry.Vector3(*angular_vel)
        )

        odom_msg = Odometry(
            header=header,
            child_frame_id='/body',
            pose=geometry.PoseWithCovariance(
                pose=pose
            ),
            twist=geometry.TwistWithCovariance(
                twist=twist
            )
        )

        self.truth_odom_pub.publish(odom_msg)

    def thrust_cb(self, msg):
        '''TODO: Clamp'''
        self.last_cmd_time = time()
        self.thrust_dict = {}
        for thrust_cmd in msg.thruster_commands:
            self.thrust_dict[thrust_cmd.name] = thrust_cmd.thrust

    def step(self, dt):
        '''Ignore dt'''
        # If timeout, reset thrust commands
        if (time() - self.last_cmd_time) > self._cmd_timeout:
            self.thrust_dict = {}

        for i, (name, rel_dir, rel_pos) in enumerate(self.thruster_list):
            thruster_force = self.thrust_dict.get(name, 0.0)
            body_force = rel_dir * thruster_force
            # print name, body_force
            self.body.addRelForceAtRelPos(body_force, rel_pos)

        self.apply_damping_force()
        self.apply_damping_torque()
        self.publish_pose()