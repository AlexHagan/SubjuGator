from txros import util
from twisted.internet import defer
import rospy
import tf
from sub8_ros_tools import normalize, clip_norm
from image_geometry import PinholeCameraModel
import numpy as np


@util.cancellableInlineCallbacks
def run(sub_singleton):
    # Right now we assume the marker is in our field of view. (Will add searching later)
    # Go to max height to see as much as we can.
    rospy.sleep(1)

    iterations = 2
    # yield sub_singleton.move.depth(.75).zero_roll_and_pitch().go()

    for i in range(iterations):
        rospy.loginfo("%d iteration." % i)
        response = yield sub_singleton.channel_marker.get_2d()

        if not response.found:
            # Handle issues
            rospy.logerr("No response")

        rel_position = yield transform_px_to_m(response, sub_singleton._tf_listener)
        print rel_position
        yield sub_singleton.move.relative([rel_position[0], rel_position[1], 0]).yaw_left(response.pose.theta).go()


@util.cancellableInlineCallbacks
def transform_px_to_m(response, tf_listener):
    '''
    Finds the position of the marker in meters relative to the current position.
    Same process as the occupacy grid maker.
    '''

    timestamp = response.header.stamp
    cam = PinholeCameraModel()
    cam.fromCameraInfo(response.camera_info)

    # Calculate position of marker accounting for camera rotation in the camera frame.
    transform = yield tf_listener.get_transform("/ground", "/downward", timestamp)
    height = transform._p[2]

    dir_vector = unit_vector(np.array([cam.cx(), cam.cy()]) - [response.pose.x, response.pose.y])

    transform = yield tf_listener.get_transform("/map", "/downward", timestamp)
    cam_rotation = tf.transformations.euler_from_quaternion(transform._q)[2] + np.pi / 2
    dir_vector = np.dot(dir_vector, make_2D_rotation(cam_rotation))

    # Calculate distance from middle of frame to marker in meters.
    mid_ray = np.array([0, 0, 1])
    marker_ray = unit_vector(cam.projectPixelTo3dRay([response.pose.x, response.pose.y]))
    theta = np.arccos(np.dot(mid_ray, marker_ray))
    magnitude = np.tan(theta) * height
    rel_position = dir_vector[::-1] * magnitude

    defer.returnValue(rel_position)


def unit_vector(vect):
    return vect / np.linalg.norm(vect)


def make_2D_rotation(angle):
    c, s = np.cos(angle), np.sin(angle)
    return np.array([[c, -s],
                     [s, c]], dtype=np.float32)