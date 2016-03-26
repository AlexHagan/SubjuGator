#include <sub8_pcl/cv_tools.hpp>

namespace sub {

cv::Point contour_centroid(Contour& contour) {
  cv::Moments m = cv::moments(contour, true);
  cv::Point center(m.m10 / m.m00, m.m01 / m.m00);
  return center;
}

bool larger_contour(const Contour &c1, const Contour &c2){
	if(cv::contourArea(c1) > cv::contourArea(c2)) return true;
	else return false;
}


cv::MatND smooth_histogram(const cv::MatND &histogram, size_t filter_kernel_size, float sigma){
	cv::MatND hist = histogram.clone();
	std::vector<float> gauss_kernel = generate_gaussian_kernel_1D(filter_kernel_size, sigma);
	size_t histSize = hist.total();
	int offset = (filter_kernel_size - 1) / 2;
	for (size_t i = offset; i < histSize-offset; i++ )   // Convolve histogram values with gaussian kernel
	{
		int sum = 0;
		int kernel_idx = 0;
		for (int j = i - offset; j <= int(i + offset); j++){
			sum += (hist.at<float>(j) * gauss_kernel[kernel_idx++]);
	    }
	    hist.at<float>(i) = sum;
	}
	for (int i = 0; i < offset; ++i)  // Pad filtered result with zeroes
	{
		hist.at<float>(i) = 0;
		hist.at<float>(histSize - 1 - i) = 0;
	}
	return hist;
}


std::vector<float> generate_gaussian_kernel_1D(size_t kernel_size, float sigma){
	std::vector<float> kernel;
	int middle_index = (kernel_size - 1) / 2;
	int first_discrete_sample_x = -(middle_index);
	for (int i = first_discrete_sample_x; i <= 0; i++)
	{
		float power = -0.5 * (float(i)/sigma) * (float(i)/sigma);
		kernel.push_back( exp(power) );				// From definition of Standard Normal Distribution
	}
	for(int i = 1; i <= middle_index; i++){			// Kernel is symmetric
		kernel.push_back(kernel[middle_index - i]);
	}
	// Normalize kernel (sum of values should equal 1.0)
	float sum = 0;
	for (size_t i = 0; i < kernel_size; i++){ sum += kernel[i];	}
	for (size_t i = 0; i < kernel_size; i++){ kernel[i] /= sum;	}
	return kernel;
}


std::vector<cv::Point> find_local_maxima(const cv::MatND &histogram, float thresh_multiplier){
	std::vector<cv::Point> local_maxima, threshed_local_maxima;
	float global_maximum = -std::numeric_limits<double>::infinity();

	// Locate local maxima and find global maximum
	for(size_t idx = 1; idx < histogram.total() - 1; idx++){
		float current_value = histogram.at<float>(idx);
	    if((histogram.at<float>(idx-1) < current_value) && (histogram.at<float>(idx+1) <= current_value)){
	    	local_maxima.push_back(cv::Point(idx, current_value));
	    	if(global_maximum < current_value) global_maximum = current_value;
	    }
	}
#ifdef SEGMENTATION_DEBUG
	std::cout << std::endl << "Maxima: ";
#endif
	BOOST_FOREACH(cv::Point pt, local_maxima){
		if(pt.y > global_maximum * thresh_multiplier) threshed_local_maxima.push_back(pt);
#ifdef SEGMENTATION_DEBUG
		std::cout << boost::format("[%1%, %2%] ") % pt.x % pt.y;
#endif
	}
#ifdef SEGMENTATION_DEBUG
	std::cout << std::endl << boost::format("thresh: > global_maximum(%1%) * thresh_multiplier(%2%) = %3%") 
								% global_maximum % thresh_multiplier % (global_maximum * thresh_multiplier);
	std::cout << std::endl << "Threshed Maxima (x): ";
	if (threshed_local_maxima.size() != local_maxima.size()){
		BOOST_FOREACH(cv::Point pt, threshed_local_maxima){
			std::cout << boost::format(" %1% ") % pt.x;
		}
	}
	else std::cout << "same as 'Maxima'";
	std::cout << std::endl;
#endif
	return threshed_local_maxima;
}


std::vector<cv::Point> find_local_minima(const cv::MatND &histogram, float thresh_multiplier){
	std::vector<cv::Point> local_minima, threshed_local_minima;
	float global_minimum = std::numeric_limits<double>::infinity();;

	// Locate local minima and find global minimum
	for(size_t idx = 1; idx < histogram.total() - 1; idx++){
		float current_value = histogram.at<float>(idx);
	    if((histogram.at<float>(idx-1) >= current_value) && (histogram.at<float>(idx+1) > current_value)){
	    	local_minima.push_back(cv::Point(idx, current_value));
	    	if(global_minimum > current_value) global_minimum = current_value;
	    }
	}
#ifdef SEGMENTATION_DEBUG
	std::cout << std::endl << "Minima: ";
#endif
	BOOST_FOREACH(cv::Point pt, local_minima){
		if(pt.y < global_minimum * thresh_multiplier) threshed_local_minima.push_back(pt);
#ifdef SEGMENTATION_DEBUG
		std::cout << boost::format("[%1%, %2%] ") % pt.x % pt.y;
#endif
	}
#ifdef SEGMENTATION_DEBUG
	std::cout << std::endl << boost::format("thresh: < global_minimum(%1%) * thresh_multiplier(%2%) = %3%") 
								% global_minimum % thresh_multiplier % (global_minimum * thresh_multiplier);
	std::cout << std::endl << "Threshed Minima (x): ";
	if (threshed_local_minima.size() != local_minima.size()){
		BOOST_FOREACH(cv::Point pt, threshed_local_minima){
			std::cout << boost::format(" %1% ") % pt.x;
		}
	}
	else std::cout << "same as 'Minima'";
	std::cout << std::endl;
#endif
	return threshed_local_minima;
}


unsigned int select_hist_mode(std::vector<cv::Point> &histogram_modes, int target){
  std::vector<int> distances;
  BOOST_FOREACH(cv::Point mode, histogram_modes){
    distances.push_back(mode.x - target);
  }
  int min_idx = 0;
  for(size_t i = 0; i < distances.size(); i++){
    if(std::abs(distances[i]) <= std::abs(distances[min_idx])) min_idx = i;
  }
  return histogram_modes[min_idx].x;
}


void statistical_image_segmentation(const cv::Mat &src, cv::Mat &dest, const int hist_size,
        							const float** ranges, const int target, std::string image_name, 
        							const float sigma, const float low_thresh_gain, const float high_thresh_gain)
{
	// Calculate histogram
	cv::MatND hist, hist_smooth, hist_derivative;
	cv::calcHist( &src, 1, 0, cv::Mat(), hist, 1, &hist_size, ranges, true, false );

	// Smooth histogram
	const int kernel_size = 11;
	hist_smooth = sub::smooth_histogram(hist, kernel_size, sigma);

	// Calculate histogram derivative (central finite difference)
	hist_derivative = hist_smooth.clone();
	hist_derivative.at<float>(0) = 0;
	hist_derivative.at<float>(hist_size - 1) = 0;
	for (int i = 1; i < hist_size - 1; ++i)
	{
		hist_derivative.at<float>(i) = (hist_smooth.at<float>(i + 1) - hist_smooth.at<float>(i - 1)) / 2.0;
	}
	hist_derivative = sub::smooth_histogram(hist_derivative, kernel_size, sigma);

	// Find target mode
#ifdef SEGMENTATION_DEBUG
	std::cout << boost::format("Target: %1%") % target;
#endif
	std::vector<cv::Point> histogram_modes = sub::find_local_maxima(hist_smooth, 0.1);
	int target_mode = sub::select_hist_mode(histogram_modes, target);
#ifdef SEGMENTATION_DEBUG
	std::cout << boost::format("Mode Selected: %1%") % target_mode;
#endif

	// Calculate std dev of histogram slopes
	cv::Scalar hist_deriv_mean, hist_deriv_stddev;
	cv::meanStdDev(hist_derivative, hist_deriv_mean, hist_deriv_stddev);

	// Determine thresholds for cv::inRange() using the std dev of histogram slopes times a gain as a cutoff heuristic
	int high_abs_derivative_thresh = std::abs(hist_deriv_stddev[0] * high_thresh_gain);
	int low_abs_derivative_thresh = std::abs(hist_deriv_stddev[0] * low_thresh_gain);
	std::vector<cv::Point> derivative_maxima = sub::find_local_maxima(hist_derivative, 0.01);
	std::vector<cv::Point> derivative_minima = sub::find_local_minima(hist_derivative, 0.01);
	int high_thresh_search_start = target_mode; int low_thresh_search_start = target_mode;
#ifdef SEGMENTATION_DEBUG
	std::cout << "high_thresh_search_start: " << target_mode << std::endl;
#endif
	for(size_t i = 0; i < derivative_minima.size(); i++){
#ifdef SEGMENTATION_DEBUG
		std::cout << boost::format("derivative_minima[%1%].x = %2%  target_mode = %3%") % i % derivative_minima[i].x %  target_mode << std::endl;
#endif
		if(derivative_minima[i].x > target_mode){
			high_thresh_search_start = derivative_minima[i].x;
#ifdef SEGMENTATION_DEBUG
			std::cout << high_thresh_search_start << " Accepted" << std::endl;
#endif
			break;
		} 
	}
#ifdef SEGMENTATION_DEBUG
	std::cout << "low_thresh_search_start: " << target_mode << std::endl;
#endif
	for(int i = derivative_maxima.size() - 1; i >= 0; i--){
#ifdef SEGMENTATION_DEBUG
		std::cout << boost::format("derivative_maxima[%1%].x = %2%  target_mode = %3%") % i % derivative_maxima[i].x %  target_mode << std::endl;
#endif
		if(derivative_maxima[i].x < target_mode){
			low_thresh_search_start = derivative_maxima[i].x;
#ifdef SEGMENTATION_DEBUG
			std::cout << low_thresh_search_start << " Accepted" << std::endl;
#endif
			break;
		}
	}
	int high_thresh = high_thresh_search_start; int low_thresh = low_thresh_search_start;
#ifdef SEGMENTATION_DEBUG
	std::cout << std::endl << "high_deriv_thresh: " << hist_deriv_stddev[0] << " * " << high_thresh_gain << " = " << high_abs_derivative_thresh << std::endl;
	std::cout << "abs(high_deriv_thresh) - abs(slope) = slope_error" << std::endl;
#endif
	for(int i = high_thresh_search_start; i < hist_size; i++){
		int abs_slope = std::abs(hist_derivative.at<float>(i));
#ifdef SEGMENTATION_DEBUG
		std::cout << "i = " << i << "  :  " << high_abs_derivative_thresh << " - " << abs_slope << " = " << high_abs_derivative_thresh - abs_slope << std::endl;
#endif
		if(abs_slope < high_abs_derivative_thresh){
			high_thresh = i;
			break;
		}
	}
#ifdef SEGMENTATION_DEBUG
	std::cout << "high_thresh = " << high_thresh << std::endl;
	std::cout << std::endl << "low_deriv_thresh: " << hist_deriv_stddev[0] << " * " << low_thresh_gain << " = " << low_abs_derivative_thresh << std::endl;
	std::cout << "abs(low_deriv_thresh) - abs(slope) = slope_error" << std::endl;
#endif
	for(int i = low_thresh_search_start; i > 0; i--){
		int abs_slope = std::abs(hist_derivative.at<float>(i));
#ifdef SEGMENTATION_DEBUG
		std::cout << "i = " << i << "  :  " << low_abs_derivative_thresh << " - " << abs_slope << " = " << high_abs_derivative_thresh - abs_slope << std::endl;
#endif
		if(abs_slope < low_abs_derivative_thresh){ 
			low_thresh = i;
			break; 
		}
	}
#ifdef SEGMENTATION_DEBUG
	std::cout << "low_thresh = " << low_thresh << std::endl;
	std::cout << std::endl;
#endif
	std::string ros_log = 
		( boost::format("Target: %1%\nClosest distribution mode: %2%  Thresholds selected:  low=%3%  high=%4%")
		% target % target_mode % low_thresh % high_thresh ).str() ;
	ROS_INFO(ros_log.c_str());

	// Threshold image
	cv::inRange(src, low_thresh, high_thresh, dest);

	// Closing Morphology operation
	int dilation_size = 2;
	cv::Mat structuring_element = cv::getStructuringElement(cv::MORPH_RECT, 
															cv::Size(2 * dilation_size + 1, 2 * dilation_size + 1),
															cv::Point(dilation_size, dilation_size) );
	cv::dilate(dest, dest, structuring_element);
	cv::erode(dest, dest, structuring_element);


#ifdef VISUALIZE
	// Prepare to draw graph of histogram and derivative
	int hist_w = 512; int hist_h = 400;
	int bin_w = cvRound( (double) hist_w/hist_size );
	cv::Mat histImage( hist_h, hist_w, CV_8UC1, cv::Scalar( 0,0,0) );
	cv::Mat histDerivImage( hist_h, hist_w, CV_8UC1, cv::Scalar( 0,0,0) );
	cv::normalize(hist_smooth, hist_smooth, 0, histImage.rows, cv::NORM_MINMAX, -1, cv::Mat() );
	cv::normalize(hist_derivative, hist_derivative, 0, histImage.rows, cv::NORM_MINMAX, -1, cv::Mat() );

	// Draw Graphs
	for( int i = 1; i < hist_size; i++ )
	{
	// Plot image histogram
	cv::line( histImage, cv::Point( bin_w*(i-1), hist_h - cvRound(hist_smooth.at<float>(i-1)) ) ,
	                 cv::Point( bin_w*(i), hist_h - cvRound(hist_smooth.at<float>(i)) ),
	                 cv::Scalar( 255, 0, 0), 2, 8, 0  );
	// Plot image histogram derivative
	cv::line( histDerivImage, cv::Point( bin_w*(i-1), hist_h - cvRound(hist_derivative.at<float>(i-1)) ) ,
	                 cv::Point( bin_w*(i), hist_h - cvRound(hist_derivative.at<float>(i)) ),
	                 cv::Scalar( 122, 0, 0), 1, 8, 0  );
	}

	// Shade in area being segmented under histogram curve
	cv::line( histImage, cv::Point( bin_w*low_thresh, hist_h - cvRound(hist_smooth.at<float>(low_thresh)) ) ,
	                 cv::Point( bin_w*low_thresh, hist_h), cv::Scalar( 125, 0, 0),2);
	cv::line( histImage, cv::Point( bin_w*high_thresh, hist_h - cvRound(hist_smooth.at<float>(high_thresh)) ) ,
                 cv::Point( bin_w*high_thresh, hist_h), cv::Scalar( 125, 0, 0),2);
	cv::floodFill(histImage, cv::Point(bin_w*cvRound(float(low_thresh + high_thresh) / 2.0), hist_h - 1), cv::Scalar(125));

	// Combine graphs into one image and display results
	cv::Mat segmentation_channel = cv::Mat::zeros(histImage.size(), CV_8UC1);
	cv::Rect upper_right_corner = cv::Rect(histImage.cols - dest.cols - 1, 0, dest.cols, dest.rows);
	dest.copyTo(segmentation_channel(upper_right_corner));
	segmentation_channel = segmentation_channel * 0.3;
	std::vector<cv::Mat> debug_img_channels;
	debug_img_channels.push_back(histImage); 
	debug_img_channels.push_back(histDerivImage);
	debug_img_channels.push_back(segmentation_channel);
	cv::Mat debug_img;
	cv::merge(debug_img_channels, debug_img);
	cv::imshow((boost::format("Statistical Image Segmentation(%1%)") % image_name).str(), debug_img );
#endif
}


Eigen::Vector3d triangulate_image_coordinates(const cv::Point &pt1, const cv::Point &pt2, 
												const Eigen::Matrix3d &fundamental, const Eigen::Matrix3d &R){
	/*
		Optimal triangulation method for two cameras with parallel principal axes
		Based of off this paper by Peter Lindstrom: https://e-reports-ext.llnl.gov/pdf/384387.pdf  **Listing 2**
	*/
	const unsigned int max_iterations = 3;
	Eigen::Vector3d p1_old(pt1.x, pt1.y, 1.0);
	Eigen::Vector3d p2_old(pt2.x, pt2.y, 1.0);
	const Eigen::Vector3d p1_0(pt1.x, pt1.y, 1.0);
	const Eigen::Vector3d p2_0(pt2.x, pt2.y, 1.0);
	Eigen::Vector3d p1, p2;
	Eigen::Vector2d n1, n2, delta_p1, delta_p2;
	Eigen::Matrix<double, 2, 3> S;
	S << 1, 0, 0,
		 0, 1, 0;
	Eigen::Matrix2d fundamental_bar = fundamental.topLeftCorner(2, 2);
	double a, b, c, d, lambda;
	c = p1_0.transpose() * (fundamental * p2_0);
	for(unsigned int i = 0; i < max_iterations; i++){
		n1 = S * (fundamental * p2_old);
		n2 = S * (fundamental.transpose() * p1_old);
		a = n1.transpose() * (fundamental_bar * n2);
		b = (0.5 * ((n1.transpose() * n1) + (n2.transpose() * n2)))(0);
		d = sqrt(b * b - a * c);
		double signum_b = (b > 0) ? 1 : ((b < 0) ? -1 : 0);
		lambda = c / (b + signum_b * d);
		delta_p1 = lambda * n1;
		delta_p2 = lambda * n2;
		p1 = p1_0 - (S.transpose() * delta_p1);
		p2 = p2_0 - (S.transpose() * delta_p2);
		p1_old = p1;
		p2_old = p2;
	}
	Eigen::Vector3d z = p1.cross(R * p2);
	Eigen::Vector3d X = ( (z.transpose() * (fundamental * p2))(0) / (z.transpose() * z)(0) ) * p1;
	return X;
}


ImageWithCameraInfo::ImageWithCameraInfo(sensor_msgs::ImageConstPtr _image_msg_ptr,
										 sensor_msgs::CameraInfoConstPtr _info_msg_ptr)
	: image_msg_ptr(_image_msg_ptr), info_msg_ptr(_info_msg_ptr), image_time(_image_msg_ptr->header.stamp) {}


FrameHistory::FrameHistory(std::string img_topic, unsigned int hist_size)
	: topic_name(img_topic), history_size(hist_size), _image_transport(nh), frame_count(0) 
{
	std::stringstream console_msg;
	console_msg << "[FrameHistory] size set to " << history_size << std::endl
				<< "\tSubscribing to image topic: " << topic_name;
	ROS_INFO(console_msg.str().c_str());
	_image_sub = _image_transport.subscribeCamera(img_topic, 1, &FrameHistory::image_callback, this);
	if(_image_sub.getNumPublishers() == 0){
		std::stringstream error_msg;
		error_msg << "[FrameHistory] no publishers currently publishing to " << topic_name;
		ROS_WARN(error_msg.str().c_str());
	}
}


FrameHistory::~FrameHistory(){
	std::stringstream console_msg;
	console_msg << "[FrameHistory] Unsubscribed from image topic: " << topic_name << std::endl
			    << "[FrameHistory] Deleting FrameHistory object" << std::endl;
	ROS_INFO(console_msg.str().c_str());
}


void FrameHistory::image_callback(const sensor_msgs::ImageConstPtr &image_msg, const sensor_msgs::CameraInfoConstPtr &info_msg){
	/**
		Adds an  ImageWithCameraInfo object to the frame history ring buffer
	*/
	ImageWithCameraInfo current_frame(image_msg, info_msg);
	bool full = _frame_history_ring_buffer.size() >= history_size;
	std::stringstream debug_msg;
	debug_msg << "Adding frame to ring buffer "
			  << "[frame=" << frame_count << "," << "full=" << (full? "true" : "false")
			  << ",frames_available=" << _frame_history_ring_buffer.size() << "]" << std::endl;
	ROS_DEBUG(debug_msg.str().c_str());
	if(!full){
		_frame_history_ring_buffer.push_back(current_frame);
	}
	else {
		_frame_history_ring_buffer[frame_count % history_size] = current_frame;
	}
	frame_count++;
}


std::vector<ImageWithCameraInfo> FrameHistory::get_frame_history(unsigned int frames_requested){
	/**
		Returns a vector with the last <num_frames> ImageWithCameraInfo objects
	*/
	std::vector<ImageWithCameraInfo> frame_history;
	std::vector<ImageWithCameraInfo> sorted_frame_history = _frame_history_ring_buffer;
	if(_frame_history_ring_buffer.size() < frames_requested){
		ROS_WARN("get_frame_history(%d): %d frames were requested, but there are %d frames available",
				 frames_requested, frames_requested, _frame_history_ring_buffer.size());
	}
	else{
		std::sort(sorted_frame_history.begin(), sorted_frame_history.end());
		for(size_t i = 0; i < frames_requested; i++){
			frame_history.push_back(sorted_frame_history[i]);
			if(i == frames_requested - 1) break;
		}
	}
	return frame_history;
}

} // namespace sub