#!/usr/bin/env python
import cv2
import numpy as np
import sys
import rospy
import image_geometry
import sub8_ros_tools
import tf
from sub8_vision_tools import threshold_tools, rviz, ProjectionParticleFilter
from sub8_msgs.srv import VisionRequest2DResponse, VisionRequest2D
from std_msgs.msg import Header
from geometry_msgs.msg import Pose2D


class BuoyFinder:
    _min_size = 50

    def __init__(self):
        self.transformer = tf.TransformListener()
        rospy.sleep(1.0)
        self.done_once = False

        self.last_image = None
        self.last_draw_image = None
        self.last_poop_image = None
        self.last_image_time = None
        self.camera_model = None
        self.last_t = None

        self.rviz = rviz.RvizVisualizer()

        self.pose_service = rospy.Service('vision/buoy/2D', VisionRequest2D, self.request_buoy)
        self.image_sub = sub8_ros_tools.Image_Subscriber('/stereo/right/image_rect_color', self.image_cb)
        self.image_pub = sub8_ros_tools.Image_Publisher('/vision/buoy_2d/target_info')

        # Occasional status publisher
        self.timer = rospy.Timer(rospy.Duration(1.0), self.publish_target_info)

        self.buoys = {
            'green': '/color/buoy/green',
            'red': '/color/buoy/red',
            'yellow': '/color/buoy/yellow',
        }

        self.ppf = None

        self.draw_colors = {
            'green': (0.0, 1.0, 0.0, 1.0),
            'red': (1.0, 0.0, 0.0, 1.0),
            'yellow': (1.0, 1.0, 0.0, 1.0),
        }

    def request_buoy(self, srv):
        print 'requesting', srv
        response = self.find_single_buoy(np.copy(self.last_image), srv.target_name)

        if response is False:
            print 'did not find'
            resp = VisionRequest2DResponse(
                header=sub8_ros_tools.make_header(frame='/stereo_front'),
                found=False
            )

        else:
            # Fill in
            center, radius = response
            resp = VisionRequest2DResponse(
                header=Header(stamp=self.last_image_time, frame_id='/stereo_front'),
                pose=Pose2D(
                    x=center[0],
                    y=center[1],
                ),
                max_x=self.last_image.shape[0],
                max_y=self.last_image.shape[1],
                camera_info=self.image_sub.camera_info,
                found=True
            )
        return resp

    def publish_target_info(self, *args):
        if self.last_image is None:
            return

        self.find_buoys(np.copy(self.last_image))
        if self.last_draw_image is not None:
            self.image_pub.publish(self.last_draw_image)

    def image_cb(self, image):
        '''Hang on to last image'''
        self.last_image = image
        self.last_image_time = self.image_sub.last_image_time
        if self.camera_model is None:
            if self.image_sub.camera_info is None:
                return

            self.camera_model = image_geometry.PinholeCameraModel()
            self.camera_model.fromCameraInfo(self.image_sub.camera_info)
            self.ppf = ProjectionParticleFilter(self.camera_model, max_Z=15, salting=0.05, jitter_prob=0)

    def ncc(self, image, mean_thresh, scale=15):
        '''Compute normalized cross correlation w.r.t a shadowed pillbox fcn

        The expected scale will vary, so we don't cache it
        '''
        kernel = np.ones((scale, scale)) * -1
        midpoint = (scale // 2, scale // 2)
        cv2.circle(kernel, midpoint, midpoint[0], 1, -1)

        mean, std_dev = cv2.meanStdDev(image)

        # Check if the scene is brighter than our a priori target
        if mean > mean_thresh:
            kernel = -kernel

        normalized_cross_correlation = cv2.filter2D((image - mean) / std_dev, -1, kernel)
        renormalized = normalized_cross_correlation
        return renormalized

    def get_biggest(self, contours):
        if len(contours) > 0:
            cnt = max(contours, key=cv2.contourArea)
            area = cv2.contourArea(cnt)
            if area > self._min_size:
                M = cv2.moments(cnt)
                cx = int(M['m10'] / M['m00'])
                cy = int(M['m01'] / M['m00'])
                tpl_center = (int(cx), int(cy))
                return cnt, tpl_center, area
        else:
            return None

    def find_single_buoy(self, img, buoy_type):
        assert buoy_type in self.buoys[buoy_type], "Buoys_2d does not know buoy color: {}".format(buoy_type)
        ncc_channel = 2
        hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)

        color_mask, _range = threshold_tools.param_thresh(img, self.buoys[buoy_type])
        bgr_range = np.average(_range, axis=1)

        hsv_vect = threshold_tools.bgr_vec_to_hsv(bgr_range)

        tries = [5, 7, 11, 15, 18, 25]
        max_area = 0
        best_ret = None
        for _try in tries:
            norc = self.ncc(hsv[::2, ::2, ncc_channel], hsv_vect[ncc_channel], scale=_try)

            ncc_mask = cv2.resize(np.uint8(norc > 0.1), None, fx=2, fy=2)
            mask = color_mask & ncc_mask

            contours, _ = cv2.findContours(mask, cv2.RETR_TREE, cv2.CHAIN_APPROX_SIMPLE)

            ret = self.get_biggest(contours)
            if ret is None:
                continue

            contour, tuple_center, area = ret
            if area > max_area:
                max_area = area
                best_ret = ret

        if best_ret is None:
            return False

        contour, tuple_center, area = best_ret
        true_center, rad = cv2.minEnclosingCircle(contour)

        if self.camera_model is not None:
            self.rviz.draw_ray_3d(tuple_center, self.camera_model, self.draw_colors[buoy_type])

            if buoy_type == 'red' and not self.done_once:
                # self.done_once = True

                (t, rot_q) = self.transformer.lookupTransform('/map', '/stereo_front/right', self.last_image_time)
                trans = np.array(t)

                if (self.last_t is None) or (np.linalg.norm(trans - self.last_t) > 0.3):
                    self.last_t = trans
                    self.ppf.set_pose(trans, sub8_ros_tools.geometry_helpers.quaternion_matrix(rot_q))
                    est, cov = self.ppf.observe(true_center)
                    self.rviz.draw_sphere(est, color=(0.9, 0.1, 0.0, 1.0), scaling=tuple(cov), frame='/map')
                    k = 6
                    for particle in self.ppf.particles[:, ::50].transpose():
                        k += 1
                        self.rviz.draw_sphere(
                            particle,
                            color=(0.8, 0.2, 0.0, 0.7),
                            scaling=(0.05, 0.05, 0.05),
                            _id=k,
                            frame='/map'
                        )

        return tuple_center, rad

    def find_buoys(self, img):
        draw_image = np.copy(img)

        # This is only run if buoy_type is not None
        for buoy_name in self.buoys.keys():
            result = self.find_single_buoy(img, buoy_name)
            if not result:
                continue

            center, rad = result
            cv2.circle(draw_image, center, int(rad), (255, 255, 0), 2)
            font = cv2.FONT_HERSHEY_SIMPLEX
            cv2.putText(draw_image, '{}'.format(buoy_name), center, font, 0.8, (20, 20, 240), 1)

        self.last_draw_image = np.copy(draw_image)


def main(args):
    bf = BuoyFinder()
    rospy.spin()

if __name__ == '__main__':
    rospy.init_node('orange_pipe_vision')
    main(sys.argv)
