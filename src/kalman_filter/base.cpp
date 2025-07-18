#include <kalman_filter/base.hpp>

#include <fstream>
#include <iomanip>

using namespace kalman_filter;

// CONSTRUCTORS
base_t::base_t(uint32_t n_variables, uint32_t n_observers)
{
    // Store dimension sizes.
    base_t::n_x = n_variables;
    base_t::n_z = n_observers;

    // Allocate prediction components.
    base_t::x.setZero(base_t::n_x);
    base_t::P.setIdentity(base_t::n_x, base_t::n_x);
    base_t::Q.setIdentity(base_t::n_x, base_t::n_x);

    // Allocate update components.
    base_t::R.setIdentity(base_t::n_z, base_t::n_z);
    base_t::z.setZero(base_t::n_z);
    base_t::S.setZero(base_t::n_z, base_t::n_z);
    base_t::C.setZero(base_t::n_x, base_t::n_z);

    // Allocate temporaries.
    base_t::t_xx.setZero(base_t::n_x, base_t::n_x);
}
base_t::~base_t()
{
    // Stop logging if running.
    base_t::stop_log();
}

// FILTER METHODS
void base_t::new_observation(uint32_t observer_index, double_t observation)
{
    // Verify index exists.
    if(!(observer_index < base_t::n_z))
    {
        throw std::runtime_error("failed to add new observation (observer_index out of range)");
    }
    
    // Store observation in the observations map.
    // NOTE: This adds or replaces the observation at the specified observer index.
    base_t::m_observations[observer_index] = observation;
}
bool base_t::has_observations() const
{
    return !base_t::m_observations.empty();
}
bool base_t::has_observation(uint32_t observer_index) const
{
    return base_t::m_observations.count(observer_index) != 0;
}
void base_t::masked_kalman_update()
{
    // Get number of observations.
    uint32_t n_o = base_t::m_observations.size();

    // Using number of observations, create masked versions of S and C.
    Eigen::MatrixXd S_m(n_o, n_o);
    Eigen::MatrixXd C_m(base_t::n_x, n_o);
    // Iterate over z indices.
    uint32_t m_i = 0;
    uint32_t m_j = 0;
    // Iterate column first.
    for(auto j = base_t::m_observations.begin(); j != base_t::m_observations.end(); ++j)
    {
        // Iterate over rows to populate S_m.
        for(auto i = base_t::m_observations.begin(); i != base_t::m_observations.end(); ++i)
        {
            // Copy the selected S element into S_m.
            S_m(m_i++, m_j) = base_t::S(i->first, j->first);
        }
        m_i = 0;

        // Copy the selected C column into C_m.
        C_m.col(m_j++) = base_t::C.col(j->first);
    }

    // Calculate inverse of masked S.
    Eigen::MatrixXd Si_m = S_m.inverse();
    
    // Calculate Kalman gain (masked by n observations).
    Eigen::MatrixXd K_m(base_t::n_x,n_o);
    K_m.noalias() = C_m * Si_m;

    // Create masked version of za-z.
    Eigen::VectorXd zd_m(n_o);
    m_i = 0;
    for(auto observation = base_t::m_observations.begin(); observation != base_t::m_observations.end(); ++observation)
    {
        zd_m(m_i++) = observation->second - base_t::z(observation->first);
    }

    // Update state.
    base_t::x.noalias() += K_m * zd_m;

    // Update covariance.
    // NOTE: Just use internal temporary since it's masked size.
    base_t::P.noalias() -= K_m * S_m * K_m.transpose();

    // Protect against non-positive definite covariance matrices.
    // Force symmetric matrix.
    base_t::t_xx = base_t::P.transpose();
    base_t::P += base_t::t_xx;
    base_t::P /= 2.0;
    // Force full rank and clean out small numbers.
    for(uint32_t i = 0; i < base_t::n_x; ++i)
    {
        double_t row_sum = 0;
        // Force symmetry of non-diagonals.
        for(uint32_t j = 0; j < base_t::n_x; ++j)
        {
            if(i!=j)
            {
                if(base_t::P(i,j) < 1E-3)
                {
                    base_t::P(i,j) = 0.0;
                }
                else
                {
                    row_sum += std::abs(base_t::P(i,j));
                }
            }
        }
        // Force PSD by controlling diagonal.
        if(base_t::P(i,i) <= row_sum)
        {
            base_t::P(i,i) = row_sum + 1E-3;
        }
    }

    // Reset observations.
    base_t::m_observations.clear();
}

// ACCESS
uint32_t base_t::n_variables() const
{
    return base_t::n_x;
}
uint32_t base_t::n_observers() const
{
    return base_t::n_z;
}
double_t base_t::state(uint32_t index) const
{
    // Check if index is valid.
    if(index >= base_t::n_x)
    {
        throw std::runtime_error("invalid state variable index");
    }

    return base_t::x(index);
}
void base_t::set_state(uint32_t index, double_t value)
{
    // Check if index is valid.
    if(index >= base_t::n_x)
    {
        throw std::runtime_error("invalid state variable index");
    }

    base_t::x(index) = value;
}
Eigen::VectorXd base_t::get_state()
{
    return base_t::x;
}
double_t base_t::covariance(uint32_t index_a, uint32_t index_b) const
{
    // Check if indices is valid.
    if(index_a >= base_t::n_x || index_b >= base_t::n_x)
    {
        throw std::runtime_error("invalid state variable index");
    }

    return base_t::P(index_a, index_b);
}
void base_t::set_covariance(uint32_t index_a, uint32_t index_b, double_t value)
{
    // Check if indices is valid.
    if(index_a >= base_t::n_x || index_b >= base_t::n_x)
    {
        throw std::runtime_error("invalid state variable index");
    }

    base_t::P(index_a, index_b) = value;
}
Eigen::MatrixXd base_t::get_covariance()
{
    return base_t::P;
}
void base_t::initialize_state(const Eigen::VectorXd& x0, const Eigen::MatrixXd& P0)
{
    if (x0.size() != static_cast<int>(n_x))
    {
        throw std::runtime_error("Initial state vector dimension does not match n_variables.");
    }
    if (P0.rows() != static_cast<int>(n_x) || P0.cols() != static_cast<int>(n_x))
    {
        throw std::runtime_error("Initial covariance matrix dimension does not match n_variables.");
    }

    x = x0;
    P = P0;
}

// LOGGING
bool base_t::start_log(const std::string& log_file, uint8_t precision)
{
    // Stop any existing log.
    base_t::stop_log();

    // Open the file for writing.
    base_t::m_log_file.open(log_file.c_str());

    // Verify that the file opened correctly.
    if(base_t::m_log_file.fail())
    {
        // Close the stream and clear flags.
        base_t::m_log_file.close();
        base_t::m_log_file.clear();

        return false;
    }

    // Set precision output for the file.
    base_t::m_log_file << std::fixed << std::setprecision(precision);

    // Write the header line.
    for(uint32_t i = 0; i < base_t::n_x; ++i)
    {
        base_t::m_log_file << "xp_" << i << ",";
    }
    for(uint32_t i = 0; i < base_t::n_z; ++i)
    {
        base_t::m_log_file << "zp_" << i << ",";
    }
    for(uint32_t i = 0; i < base_t::n_z; ++i)
    {
        base_t::m_log_file << "za_" << i << ",";
    }
    for(uint32_t i = 0; i < base_t::n_x; ++i)
    {
        base_t::m_log_file << "xe_" << i;
        if(i + 1 < base_t::n_x)
        {
            base_t::m_log_file << ",";
        }
    }
    base_t::m_log_file << std::endl;

    return true;
}
void base_t::stop_log()
{
    // Check if a log is running.
    if(base_t::m_log_file.is_open())
    {
        // Close the stream and reset flags.
        base_t::m_log_file.close();
        base_t::m_log_file.clear();
    }
}
void base_t::log_predicted_state()
{
    if(base_t::m_log_file.is_open())
    {
        for(uint32_t i = 0; i < base_t::n_x; ++i)
        {
            base_t::m_log_file << base_t::x(i) << ",";
        }
    }
}
void base_t::log_observations(bool empty)
{
    if(base_t::m_log_file.is_open())
    {
        if(empty)
        {
            for(uint32_t i = 0; i < 2*base_t::n_z; ++i)
            {
                base_t::m_log_file << ",";
            }
        }
        else
        {
            // Predicted observations.
            for(uint32_t i = 0; i < base_t::n_z; ++i)
            {
                base_t::m_log_file << base_t::z(i) << ",";
            }
            // Actual observations.
            for(uint32_t i = 0; i < base_t::n_z; ++i)
            {
                auto observation = base_t::m_observations.find(i);
                if(observation != base_t::m_observations.end())
                {
                    base_t::m_log_file << observation->second;
                }
                base_t::m_log_file << ",";
            }
        }
    }
}
void base_t::log_estimated_state()
{
    if(base_t::m_log_file.is_open())
    {
        for(uint32_t i = 0; i < base_t::n_x; ++i)
        {
            base_t::m_log_file << base_t::x(i);
            if(i + 1 < base_t::n_x)
            {
                base_t::m_log_file << ",";
            }
        }
        base_t::m_log_file << std::endl;
    }
}