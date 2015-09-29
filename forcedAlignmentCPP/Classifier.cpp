/************************************************************************
Project:  Text Alignment
Module:   Classifier
Purpose:  Segmentation classifier
Programmers: Joseph Keshet & Rafi Cohen

**************************** INCLUDE FILES *****************************/
#include "Classifier.h"
#include "array3dim.h"
#include <iostream>

#define GAMMA_EPSILON 1
#define NORM_TYPE1 // normalize each phoneme in phi_0 by its num of frames
//#define NORM_TYPE2
#define NORM_SCORES_0_1
#define _min(a,b) (((a)<(b))?(a):(b))

int Classifier::m_phi_size = 3;
std::string Classifier::m_loss_type;

/************************************************************************
Function:     Classifier::Classifier

Description:  Constructor
Inputs:       none.
Output:       none.
Comments:     none.
***********************************************************************/
Classifier::Classifier(unsigned int _sbin, double _min_char_length,
	double _max_char_length, double _min_sqrt_gamma, std::string _loss_type) :
	m_sbin(_sbin),
	m_w(Mat::zeros(m_phi_size,1, CV_64F)),
	m_w_old(Mat::zeros(m_phi_size, 1, CV_64F)),
	m_min_sqrt_gamma(_min_sqrt_gamma)
{

	m_min_num_cells = int(_min_char_length / double(m_sbin));
	m_max_num_cells = int(_max_char_length / double(m_sbin));

	if (_loss_type != "tau_insensitive_loss" && _loss_type != "alignment_loss") {
		std::cout << "Error: undefined loss type" << std::endl;
		exit(-1);
	}
	std::cout << "Info: training using " << _loss_type << "." << std::endl;
	m_loss_type = _loss_type;
}

/************************************************************************
Function:     Classifier::load_phoneme_stats

Description:  Load phoneme statistics
Inputs:       none.
Output:       none.
Comments:     none.
***********************************************************************/
void Classifier::load_char_stats(const CharClassifier& lm)
{
	lm.load_char_stats(m_char_length_mean, m_char_length_std);
}

/************************************************************************
Function:     Classifier::update

Description:  Train classifier with one example
Inputs:       infra::vector& x - example instance
int y - label
Output:       double - squared loss
Comments:     none.
***********************************************************************/
double Classifier::update(AnnotatedLine& x, StartTimeSequence &y,
	StartTimeSequence &y_hat)
{
	double loss = 0.0;

	loss = sqrt(gamma(y, y_hat));

	std::cout << "sqrt(gamma) = " << loss << std::endl;

	if (loss < m_min_sqrt_gamma) {
		m_w_changed = false;
		return 0.0;
	}

	Mat delta_phi = Mat::zeros(m_phi_size, 1, CV_64F);
	

	Mat phi_x_y = phi(x, y);
	Mat phi_x_y_hat = phi(x, y_hat);

	delta_phi = phi_x_y - phi_x_y_hat;
	delta_phi /= y.size();
	loss -= m_w.dot(delta_phi);

	std::cout << "phi(x,y)= " << phi_x_y << std::endl;
	std::cout << "phi(x,y_hat)= " << phi_x_y_hat << std::endl;
	std::cout << "delta_phi = " << delta_phi << std::endl;
	std::cout << "delta_phi.norm2() = " << norm(delta_phi) << std::endl;
	std::cout << "loss(PA) = " << loss << std::endl;

	if (loss < 0.0) {
		std::cerr << "Error: loss is less than 0" << std::endl;
	}
	else {
		// keep old w
		m_w_old = m_w;
		// update
		double tau = loss / norm(delta_phi);
		
		//if (tau > PA1_C) tau = PA1_C; // PA-I
		
		std::cout << "tau(PA) = " << tau << std::endl;
		delta_phi *= tau;
		m_w += delta_phi;
	}

	std::cout << "w = " << m_w << std::endl;
	m_w_changed = true;

	return loss;
}

/************************************************************************
Function:     phi

Description:  calculate phi of x for update
Inputs:       AnnotatedLine &x, StartTimeSequence &y
Output:       Mat
Comments:     none.
***********************************************************************/
Mat Classifier::phi(AnnotatedLine& x, StartTimeSequence& y)
{
	Mat v = Mat::zeros(m_phi_size, 1, CV_64F);
	for (int i = 0; i < int(y.size()); ++i) 
	{
		int char_end_at;
		if (i == int(y.size()) - 1)
		{
			char_end_at = x.m_bW;
		}
		else
		{
			char_end_at = y[i + 1] - 1;
		}
		
		Mat phi1 = phi_1(x, i, char_end_at, char_end_at - y[i] + 1);
		v.rowRange(0, m_phi_size - 1) += phi1;
		
		if (i > 0)
		{
			v.at<double>(m_phi_size-1) += phi_2(x, i, char_end_at, char_end_at - y[i] + 1, y[i] - y[i - 1]);
		}
	}
	return v;
}

/************************************************************************
Function:     phi_1

Description:  calculate static part of phi for inference
Inputs:       AnnotatedLine &x - hog features
int i - char index
int t - char end time
int l - char length
Output:       Mat
Comments:     none.
***********************************************************************/
Mat Classifier::phi_1(AnnotatedLine& x,
	int i, // char index
	int t, // char end time
	int l) // char length
{
	Mat v = Mat::zeros(m_phi_size-1, 1, CV_64F);

	uchar asciiCode = x.m_charSeq[i];

	double score = x.returnScore(asciiCode, t - l + 1, t);
	v.at<double>(0) = score;
	// TODO: fix this mean and std.
	double length_mean = m_char_length_mean[asciiCode];
	double lenth_std = m_char_length_std[asciiCode];
	v.at<double>(1) = gaussian(l*m_sbin, length_mean, lenth_std);

	return (v / double(x.m_charSeq.size()));
}

/************************************************************************
Function:     phi_2

Description:  calculate dynamic part of phi for inference
Inputs:       SpeechUtterance &x - raw features
int i - phoneme index
int t - phoneme end time
int l1 - phoneme length
int l2 - previous phoneme length
Output:       infra::vector_view
Comments:     none.
***********************************************************************/
double Classifier::phi_2(AnnotatedLine& x,
	int i, // char index
	int t, // char end time
	int l1, // char length
	int l2) // previous char length
{
	double v = 0;

	uchar currAsciiCode = x.m_charSeq[i];
	uchar prevAsciiCode = x.m_charSeq[i - 1];

	double prevMean = m_char_length_mean[currAsciiCode];
	double currMean = m_char_length_mean[prevAsciiCode];

	v = (double(l1) / prevMean -
		double(l2) / currMean);
	v *= v;
	
	return (v / double(x.m_charSeq.size()));
}

/************************************************************************
Function:     Classifier::predict

Description:  Predict label of instance x
Inputs:       AnnotatedLine &x
StartTimeSequence &y_hat
Output:       void
Comments:     none.
***********************************************************************/

double Classifier::predict(AnnotatedLine& x, StartTimeSequence &y_hat)
{
	// predict label - the argmax operator
	int C = x.m_charSeq.size();
	int T = x.m_bW;
	int L = m_max_num_cells + 1;
	threeDimArray<int> prev_l(C, T, L); // the value of l2 for back-tracking
	threeDimArray<int> prev_t(C, T, L); // the value of t2 for back-tracking
	threeDimArray<double> D0(C, T, L); // char i finished at time t and started at t-l+1
	double D1, D2, D2_max; // helper variables

	// Initialization
	D0.init(MISPAR_KATAN_MEOD);

	// Here calculate the calculation for the culculata
	for (int t = m_min_num_cells; t < _min(T, m_max_num_cells); ++t)
	{
		D1 = m_w.rowRange(0, m_phi_size - 1).dot(phi_1(x, 0, t, t + 1));
		D0(0, t, t + 1) = D1;
	}

	// The assumption is that two consecutive chars span the following interval [t-l1-l2+1,t-l1],[t-l1,t], 
	// which have lengths l2 and l1 respectively.

	// Recursion
	for (int i = 1; i < C; i++) {
		for (int t = m_min_num_cells; t < T; ++t) {
			int stop_l1_at = (t < _min(T, m_min_num_cells)) ? t : _min(T, m_max_num_cells);
			for (int l1 = m_min_num_cells; l1 <= stop_l1_at; ++l1) 
			{
				D1 = m_w.rowRange(0, m_phi_size - 1).dot(phi_1(x, i, t, l1));
				D2_max = MISPAR_KATAN_MEOD;
				for (int l2 = m_min_num_cells; l2 <= _min(T, m_max_num_cells); ++l2)
				{
					D2 = D0(i - 1, t - l1, l2) + m_w.at<double>(m_phi_size - 1)*phi_2(x, i, t, l1, l2);
					if (D2 >= D2_max) 
					{
						D2_max = D2;
						prev_l(i, t, l1) = l2;
						prev_t(i, t, l1) = t - l1;
					}
				}
				D0(i, t, l1) = D1 + D2_max;
			}
		}
	}

	// Termination
	std::vector<int> pred_l(C);
	std::vector<int> pred_t(C);
	D2_max = MISPAR_KATAN_MEOD;
	for (int l = m_min_num_cells; l <= _min(T, m_max_num_cells); l++) {
		if (D0(C - 1, T - 1, l) > D2_max) {
			D2_max = D0(C - 1, T - 1, l);
			pred_l[C - 1] = l;
			pred_t[C - 1] = T - 1;
		}
	}
	y_hat[C - 1] = T - 1 - pred_l[C - 1] + 1;

	// Back-tracking
	for (short p = C - 2; p >= 0; p--) {
		pred_l[p] = prev_l(p + 1, pred_t[p + 1], pred_l[p + 1]);
		pred_t[p] = prev_t(p + 1, pred_t[p + 1], pred_l[p + 1]);
		y_hat[p] = pred_t[p] - pred_l[p] + 1;
	}
	y_hat[0] = 0;

	return (D2_max / double(T));
}



/************************************************************************
Function:     Classifier::aligned_phoneme_scores

Description:  Compute the score of the pronounced phonemes x given alignment y
Inputs:       SpeechUtterance &x
StartTimeSequence &y
Output:       score
Comments:     none.
***********************************************************************/
/*
double Classifier::aligned_phoneme_scores(SpeechUtterance& x, StartTimeSequence &y)
{
	double score = 0.0;

	// run over all phoneme except the last one
	for (int i = 0; i < y.size() - 1; i++)
		for (int t = y[i]; t < y[i + 1] - 1; t++)
			score += x.original_scores(t, x.phonemes[i]);

	// last phoneme
	for (int t = y[y.size() - 1]; t < x.original_scores.height(); t++)
		score += x.original_scores(t, x.phonemes[y.size() - 1]);

	return score;
}
*/

/************************************************************************
Function:     Classifier::align_keyword

Description:  Predict label of instance x
Inputs:       SpeechUtterance &x
StartTimeSequence &y_hat
Output:       void
Comments:     none.
***********************************************************************/
/*
double Classifier::align_keyword(AnnotatedLine& x, StartTimeSequence &y_hat_best,
	int &end_frame_best)
{
	// predict label - the argmax operator
	int P = x.phonemes.size();
	int T = x.scores.height();
	int L = _min(T, max_num_frames) + 1;
	threeDimArray<int> prev_l(P, T, L);
	threeDimArray<int> prev_t(P, T, L);
	threeDimArray<double> D0(P, T, L);
	double D1, D2, D2_max = MISPAR_KATAN_MEOD, D0_best; // helper variables
	D0_best = MISPAR_KATAN_MEOD;
	std::vector<int> pred_l(P);
	std::vector<int> pred_t(P);
	int best_s = 0;
	StartTimeSequence y_hat;
	y_hat.resize(x.phonemes.size());
	y_hat_best.resize(x.phonemes.size());
	int end_frame;

	for (int s = 0; s < T - P*min_num_frames; s++) {
		// Initialization
		for (int i = 0; i < P; i++)
			for (int t = 0; t < T; t++)
				for (int l1 = 0; l1 < L; l1++)
					D0(i, t, l1) = MISPAR_KATAN_MEOD;

		// Here calculate the calculation for the culculata
		for (int t = s + min_num_frames; t < _min(s + max_num_frames, T); t++) {
			D0(0, t, t - s + 1) = w.subvector(0, phi_size - 1) * phi_1(x, 0, t, t - s + 1);
		}

		// Recursion
		for (int i = 1; i < P; i++) {
			for (int t = s + i*min_num_frames; t < _min(s + i*max_num_frames, T); t++) {
				int stop_l1_at = (t < _min(T, max_num_frames)) ? t : _min(T, max_num_frames);
				for (int l1 = min_num_frames; l1 <= stop_l1_at; l1++) {
					D1 = w.subvector(0, phi_size - 1) * phi_1(x, i, t, l1);
					D2_max = MISPAR_KATAN_MEOD;
					for (int l2 = min_num_frames; l2 <= _min(T, max_num_frames); l2++) {
						D2 = D0(i - 1, t - l1, l2) + w(phi_size - 1) * phi_2(x, i, t, l1, l2);
						if (D2 > D2_max) {
							D2_max = D2;
							prev_l(i, t, l1) = l2;
							prev_t(i, t, l1) = t - l1;
						}
					}
					D0(i, t, l1) = D1 + D2_max;
				}
			}
		}

		// Termination
		D2_max = MISPAR_KATAN_MEOD;
		for (int t = s + (P - 1)*min_num_frames; t<_min(s + (P - 1)*max_num_frames, T); t++)  {
			for (int l = min_num_frames; l <= _min(T, max_num_frames); l++) {
				if (D0(P - 1, t, l) > D2_max) {
					D2_max = D0(P - 1, t, l);
					pred_l[P - 1] = l;
					pred_t[P - 1] = t;
				}
			}
		}
		y_hat[P - 1] = pred_t[P - 1] - pred_l[P - 1] + 1;
		// Back-tracking
		for (short p = P - 2; p >= 0; p--) {
			pred_l[p] = prev_l(p + 1, pred_t[p + 1], pred_l[p + 1]);
			pred_t[p] = prev_t(p + 1, pred_t[p + 1], pred_l[p + 1]);
			y_hat[p] = pred_t[p] - pred_l[p] + 1;
		}
		y_hat[0] = s;
		end_frame = pred_t[P - 1];

		// apply normalization
#ifdef NORM_TYPE2
		D2_max /= (end_frame - s + 1);
#endif		
		//		std::cout << "y_hat=" << y_hat << " " << pred_t[P-1] << std::endl;
		//		std::cout << "w*phi(predicted)=" << w*phi(x,keyword, y_hat, pred_t[P-1]) << std::endl;
		//		std::cout << "D2_max=" << D2_max << std::endl;

		if (D2_max > D0_best) {
			//std::cout << "s=" << s << " D2_max=" << D2_max << " D0_best=" << D0_best << std::endl;
			D0_best = D2_max;
			best_s = s;
			for (int i = 0; i < P; i++)
				y_hat_best[i] = y_hat[i];
			end_frame_best = end_frame;
		}
	}
	return D0_best;
}
*/

/************************************************************************
Function:     gamma

Description:  Distance between two labels
Inputs:       StartTimeSequence &y
StartTimeSequence &y_hat
Output:       double - the resulted distance
Comments:     none.
***********************************************************************/
double Classifier::gamma(const StartTimeSequence &y, const StartTimeSequence &y_hat)
{
	double loss = 0.0;

	if (m_loss_type == "tau_insensitive_loss")  {

		for (unsigned long i = 0; i < y.size(); ++i) {
			double loss_i = fabs(double(y_hat[i]) - double(y[i])) - GAMMA_EPSILON;
			if (loss_i > 0.0) loss += loss_i;
		}

	}
	else if (m_loss_type == "alignment_loss")  {

		for (unsigned long i = 0; i < y.size(); ++i) {
			loss += (fabs(double(y_hat[i]) - double(y[i])) > GAMMA_EPSILON) ? 1.0 : 0.0;
		}

	}
	else {
		std::cerr << "Error: loss type \"" << m_loss_type << "\" is undefined." << std::endl;
		exit(-1);
	}
	return loss / double(y.size());

}

/************************************************************************
Function:     gamma

Description:  Distance between two labels
Inputs:       StartTimeSequence &y
StartTimeSequence &y_hat
Output:       double - the resulted distance
Comments:     none.
***********************************************************************/
double Classifier::gamma(const int y, const int y_hat)
{
	if (m_loss_type == "tau_insensitive_loss")  {

		double loss_i = fabs(double(y_hat) - double(y)) - GAMMA_EPSILON;
		if (loss_i > 0.0)
			return loss_i;

	}
	else if (m_loss_type == "alignment_loss")  {

		double loss_i = (fabs(double(y_hat) - double(y)) > GAMMA_EPSILON) ? 1.0 : 0.0;
		return loss_i;

	}
	else {
		std::cerr << "Error: loss type \"" << m_loss_type << "\" is undefined." << std::endl;
		exit(-1);
	}

	return 0.0;
}


/************************************************************************
Function:     gaussian

Description:  Gaussian PDF
Inputs:       double x, double mean, double std
Output:       double.
Comments:     none.
***********************************************************************/
double Classifier::gaussian(const double x, const double mean, const double std)
{
	double d = (1 / sqrt(2 * 3.141529) / std * exp(-((x - mean)*(x - mean)) / (2 * std*std)));
	return (d);
}

/************************************************************************
Function:     Classifier::load

Description:  Loads a classifier
Inputs:       string & filename
Output:       none.
Comments:     none.
***********************************************************************/
void Classifier::load(std::string &filename)
{
	std::ifstream ifs;
	ifs.open(filename.c_str());
	if (!ifs.good()) {
		std::cerr << "Unable to open " << filename << std::endl;
		exit(-1);
	}

	//ifs >> m_w;

	ifs.close();
}

/************************************************************************
Function:     Classifier::save

Description:  Saves a classifier
Inputs:       string & filename
Output:       none.
Comments:     none.
***********************************************************************/
void Classifier::save(std::string &filename)
{
	std::ofstream ifs;
	ifs.open(filename.c_str());
	if (!ifs.good()) {
		std::cerr << "Unable to open " << filename << std::endl;
		exit(-1);
	}

	ifs << m_w;

	ifs.close();
}



// --------------------- EOF ------------------------------------//
