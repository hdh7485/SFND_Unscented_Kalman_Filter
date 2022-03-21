#include "ukf.h"
#include "Eigen/Dense"
#include <iostream>

using Eigen::MatrixXd;
using Eigen::VectorXd;

UKF::UKF() {

  // if this is false, laser measurements will be ignored (except during init)
  use_laser_ = true;

  // if this is false, radar measurements will be ignored (except during init)
  use_radar_ = true;

  // initial state vector
  x_ = VectorXd(5);

  // initial covariance matrix
  P_ = MatrixXd(5, 5);

  // process noise standard deviation longitudinal acceleration in m/s^2
  std_a_ = 2;

  // process noise standard deviation yaw acceleration in rad/s^2
  std_yawdd_ = 1;
  
  /* SENSOR DATA */

  // laser measurement noise standard deviation position1 in m
  std_laspx_ = 0.15;

  // laser measurement noise standard deviation position2 in m
  std_laspy_ = 0.15;

  // radar measurement noise standard deviation radius in m
  std_radr_ = 0.3;

  // radar measurement noise standard deviation angle in rad
  std_radphi_ = 0.03;

  // radar measurement noise standard deviation radius change in m/s
  std_radrd_ = 0.3;
  
  /* END OF SENSOR DATA */
  
  is_initialized_ = false;
  
  /**
   * End DO NOT MODIFY section for measurement noise values
   */

  /**
   * TODO: Complete the initialization. See ukf.h for other member properties.
   * Hint: one or more values initialized above might be wildly off...
   */

  // state dimension
  n_x_ = 5;

  // augmented state dimension
  n_aug_ = 7;

  // define spreading parameter
  lambda_ = 3 - n_aug_;

  // matrix to hold sigma points
  Xsig_pred_ = MatrixXd(n_x_, 2 * n_aug_ + 1);

  // vector for weights
  weights_ = VectorXd(2 * n_aug_ + 1);

  // start time
  time_us_ = 0;

  NIS_lidar_ = 0.0;
  NIS_radar_ = 0.0;
}

UKF::~UKF() {}

void UKF::ProcessMeasurement(MeasurementPackage meas_package) {
  /**
   * TODO: Complete this function! Make sure you switch between lidar and radar
   * measurements.
   */
  if (!is_initialized_) {
    if (meas_package.sensor_type_ == MeasurementPackage::LASER) {
      float x = meas_package.raw_measurements_(0);
      float y = meas_package.raw_measurements_(1);

      x_ << x, y, 0, 0, 0;
      P_.setIdentity();
    } else if (meas_package.sensor_type_ == MeasurementPackage::RADAR) {
      float rho = meas_package.raw_measurements_(0);
      float phi = meas_package.raw_measurements_(1);
      float rho_dot = meas_package.raw_measurements_(2);

      float x = rho*sin(phi);
      float y = rho*cos(phi);

      x_<< x, y, rho_dot, phi, 0;
      P_.setIdentity();
    }
    is_initialized_ = true;
    time_us_ = meas_package.timestamp_;
  }

  double delta_t = (meas_package.timestamp_ - time_us_) / 1000000.0;
  time_us_ = meas_package.timestamp_;

  Prediction(delta_t);

  if (meas_package.sensor_type_ == MeasurementPackage::LASER) {
    UpdateLidar(meas_package);
  } else if (meas_package.sensor_type_ == MeasurementPackage::RADAR) {
    UpdateRadar(meas_package);
  }
}

void UKF::Prediction(double delta_t) {
  /**
   * TODO: Complete this function! Estimate the object's location.
   * Modify the state vector, x_. Predict sigma points, the state,
   * and the state covariance matrix.
   */
  VectorXd x_aug = VectorXd(n_aug_).setZero();
  MatrixXd P_aug = MatrixXd(n_aug_, n_aug_).setZero();
  MatrixXd Xsig_aug = MatrixXd(n_aug_, 2 * n_aug_ + 1).setZero();

  x_aug.head(5) = x_;
  P_aug.topLeftCorner(n_x_, n_x_) = P_;
  P_aug(n_x_, n_x_) = std_a_ * std_a_;
  P_aug(n_x_ + 1, n_x_ + 1) = std_yawdd_ * std_yawdd_;
  MatrixXd A = P_aug.llt().matrixL();
  Xsig_aug.col(0) = x_aug;
  for (int i = 0; i < n_aug_; i++) {
    Xsig_aug.col(i + 1) = x_aug + sqrt(lambda_ + n_aug_) * A.col(i);
    Xsig_aug.col(i + 1 + n_aug_) = x_aug - sqrt(lambda_ + n_aug_) * A.col(i);
  }

  for (int i = 0 ; i < 2 * n_aug_ + 1; i++) {
    double px = Xsig_aug(0, i);
    double py = Xsig_aug(1, i);
    double v = Xsig_aug(2, i);
    double yaw = Xsig_aug(3, i);
    double yawd = Xsig_aug(4, i);
    double nu_a = Xsig_aug(5, i);
    double nu_yawdd = Xsig_aug(6, i);

    // predicted state values
    double px_p, py_p, v_p, yaw_p, yawd_p;

    // avoid division by zero
    if (fabs(yawd) > 0.001) {
      px_p = px + v / yawd * (sin(yaw + yawd * delta_t) - sin(yaw));
      py_p = py + v / yawd * (cos(yaw) - cos(yaw + yawd * delta_t));
    } else {
      px_p = px + v * delta_t * cos(yaw);
      py_p = py + v * delta_t * sin(yaw);
    }

    v_p = v;
    yaw_p = yaw + yawd * delta_t;
    yawd_p = yawd;

    // add noise
    px_p = px_p + 0.5 * nu_a * delta_t * delta_t * cos(yaw);
    py_p = py_p + 0.5 * nu_a * delta_t * delta_t * sin(yaw);
    v_p = v_p + nu_a * delta_t;
    yaw_p = yaw_p + 0.5 * nu_yawdd * delta_t * delta_t;
    yawd_p = yawd_p + nu_yawdd * delta_t;

    Xsig_pred_(0, i) = px_p;
    Xsig_pred_(1, i) = py_p;
    Xsig_pred_(2, i) = v_p;
    Xsig_pred_(3, i) = yaw_p;
    Xsig_pred_(4, i) = yawd_p;  
  }

  double weight_0 = lambda_ / (lambda_ + n_aug_);
  weights_(0) = weight_0;
  
  for (int i = 1; i < 2 * n_aug_ + 1; i++) {
    weights_(i) = 0.5 / (lambda_ + n_aug_);;
  }
  x_.fill(0.0);
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {
    x_ = x_ + weights_(i) * Xsig_pred_.col(i);
  }
  P_.fill(0.0);
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {
    VectorXd x_diff = Xsig_pred_.col(i) - x_;
    while (x_diff(3) > M_PI) {
      x_diff(3) -= 2.0 * M_PI;
    }
    while (x_diff(3) < -M_PI) {
      x_diff(3) += 2.0 * M_PI;
    }
    P_ = P_ + weights_(i) * x_diff * x_diff.transpose();
  }
}

void UKF::UpdateLidar(MeasurementPackage meas_package) {
  /**
   * todo: complete this function! use lidar data to update the belief
   * about the object's position. modify the state vector, x_, and
   * covariance, p_.
   * You can also calculate the lidar NIS, if desired.
   */
  int n_z = 2;
  MatrixXd Zsig = MatrixXd(n_z, 2 * n_aug_ + 1);
  VectorXd z_pred = VectorXd(n_z);
  MatrixXd S = MatrixXd(n_z, n_z);
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {
    Zsig(0, i) = Xsig_pred_(0, i);
    Zsig(1, i) = Xsig_pred_(1, i);
  }

  z_pred.fill(0.0);
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {
    z_pred = z_pred + weights_(i) * Zsig.col(i);
  }

  S.fill(0.0);
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {
    VectorXd z_diff = Zsig.col(i) - z_pred;
    S = S + weights_(i) * z_diff * z_diff.transpose();
  }
  MatrixXd R = MatrixXd(n_z, n_z);
  R << std_laspx_ * std_laspx_, 0,
       0, std_laspy_ * std_laspy_;
  S = S + R;

  MatrixXd Tc = MatrixXd(n_x_, n_z);

  Tc.fill(0.0);
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {
    VectorXd z_diff = Zsig.col(i) - z_pred;
    VectorXd x_diff = Xsig_pred_.col(i) - x_;
    while (x_diff(3) > M_PI) {
      x_diff(3) -= 2.0 * M_PI;
    }
    while (x_diff(3) < -M_PI) {
      x_diff(3) += 2.0 * M_PI;
    }
    Tc = Tc + weights_(i) * x_diff * z_diff.transpose();
  }

  MatrixXd K = Tc * S.inverse();
  VectorXd z = meas_package.raw_measurements_;
  VectorXd z_diff = z - z_pred;

  x_ = x_ + K * z_diff;
  P_ = P_ - K * S * K.transpose();

  NIS_lidar_ = z_diff.transpose() * S.inverse() * z_diff;
  std::cout << "LiDAR NIS: " << NIS_lidar_ << std::endl;
}

void UKF::UpdateRadar(MeasurementPackage meas_package) {
  /**
   * TODO: Complete this function! Use radar data to update the belief
   * about the object's position. Modify the state vector, x_, and
   * covariance, P_.
   * You can also calculate the radar NIS, if desired.
   */
  int n_z = 3;
  MatrixXd Zsig = MatrixXd(n_z, 2 * n_aug_ + 1);
  VectorXd z_pred = VectorXd(n_z);
  MatrixXd S = MatrixXd(n_z, n_z);
  Zsig.fill(0.0);
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {
    double px  = Xsig_pred_(0, i);
    double py  = Xsig_pred_(1, i);
    double v   = Xsig_pred_(2, i);
    double yaw = Xsig_pred_(3, i);

    double v1 = cos(yaw) * v;
    double v2 = sin(yaw) * v;

    Zsig(0, i) = sqrt(px * px + py * py);
    Zsig(1, i) = atan2(py, px);
    Zsig(2, i) = (px * v1 + py * v2) / sqrt(px * px + py * py);
  }

  z_pred.fill(0.0);
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {
    z_pred = z_pred + weights_(i) * Zsig.col(i);
  }

  S.fill(0.0);
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {
    VectorXd z_diff = Zsig.col(i) - z_pred;
    while (z_diff(1) > M_PI) {
      z_diff(1) -= 2. * M_PI;
    }
    while (z_diff(1) < -M_PI) {
      z_diff(1) += 2. * M_PI;
    }
    S = S + weights_(i) * z_diff * z_diff.transpose();
  }

  MatrixXd R = MatrixXd(n_z, n_z);
  R << std_radr_ * std_radr_, 0, 0,
      0, std_radphi_ * std_radphi_, 0,
      0, 0, std_radrd_ * std_radrd_;

  S = S + R;
  
  MatrixXd Tc = MatrixXd(n_x_, n_z);
  Tc.fill(0.0);
  for (int i = 0; i < 2 * n_aug_ + 1; i++) {
    VectorXd z_diff = Zsig.col(i) - z_pred;
    while (z_diff(1) > M_PI) {
      z_diff(1) -= 2.0 * M_PI;
    }
    while (z_diff(1) < -M_PI) {
      z_diff(1) += 2.0 * M_PI;
    }

    VectorXd x_diff = Xsig_pred_.col(i) - x_;
    while (x_diff(3) > M_PI) {
      x_diff(3) -= 2.0 * M_PI;
    }
    while (x_diff(3) < -M_PI) {
      x_diff(3) += 2.0 * M_PI;
    }
    Tc = Tc + weights_(i) * x_diff * z_diff.transpose();
  }

  MatrixXd K = Tc * S.inverse();
  VectorXd z = meas_package.raw_measurements_;
  VectorXd z_diff = z - z_pred;
  while (z_diff(1) > M_PI) {
    z_diff(1) -= 2.0 * M_PI;
  }
  while (z_diff(1) < -M_PI) {
    z_diff(1) += 2.0 * M_PI;
  }
  x_ = x_ + K * z_diff;
  P_ = P_ - K * S * K.transpose();
  NIS_radar_ = z_diff.transpose() * S.inverse() * z_diff;
  std::cout << "Radar NIS: " << NIS_radar_ << std::endl;
}