#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lmcell {

struct Matrix {
  std::size_t rows = 0;
  std::size_t cols = 0;
  std::vector<double> values;
  double at(std::size_t row, std::size_t col) const;
};

struct Substrate {
  std::string name;
  double diffusion = 0.0;
  double decay = 0.0;
  double initial = 0.0;
  double boundary = -1.0;
};

// Per-side override of a substrate's edge condition: value -1 is no flux,
// otherwise the edge voxels on that side are pinned to the value.
struct Edge {
  std::string substrate;
  std::string side; // xmin, xmax, ymin or ymax
  double value = -1.0;
};

struct Exchange {
  std::string substrate;
  double uptake = 0.0;
  double secretion = 0.0;
};

struct CellType {
  std::string name = "cell";
  double cycle_rate = 0.0;
  double death_rate = 0.0;
  double motility_speed = 0.0;
  double persistence_time = 1.0;
  double radius = 8.0;
  std::vector<Exchange> exchanges; // at most one entry per substrate
  std::string chemotaxis;          // substrate whose gradient biases motility
  double chemotaxis_bias = 0.0;    // 0 unbiased .. 1 straight up the gradient
};

struct Rule {
  std::string substrate;
  std::string effect;
  std::string target;
  double half_max = 1.0;
  double hill = 1.0;
  std::size_t type = 0;
  // effect "toward": rate = base + (saturation - base) * H (PhysiCell rules).
  double saturation = 0.0;
};

// Signal name for rules that respond to crowding instead of a substrate: the
// number of other live cells in the same voxel at the start of the step.
inline constexpr const char *kNeighbors = "neighbors";

struct Config {
  double xmin = -100.0;
  double xmax = 100.0;
  double ymin = -100.0;
  double ymax = 100.0;
  double voxel_size = 20.0;
  double duration = 0.0;
  double dt = 1.0;
  std::uint64_t seed = 1;
  std::vector<Substrate> substrates;
  std::vector<CellType> types;
  std::vector<Rule> rules;
  std::vector<std::vector<double>> seeds;
  std::vector<Edge> edges;
  double record_interval = 0.0; // 0: no history
  // Largest diffusion/exchange substep; 0 uses the cell step. Edge-pinned
  // and uptake-dominated fields converge as D*dt/h^2 and uptake*dt shrink.
  double diffusion_dt = 0.0;
};

struct Results {
  Matrix counts;   // time, live cells
  Matrix cells;    // final x,y,type,alive
  Matrix profiles; // voxel x,y,substrate concentrations
  Matrix type_counts;     // time, live cells of each type
  Matrix history;         // time,x,y,type,alive at each recorded time
  Matrix profile_history; // time,x,y,substrates at each recorded time
  std::vector<std::string> type_names;
  std::vector<std::string> substrate_names;
};

// PhysiCell/BioFVM are process-global by design. A process may execute one
// simulation; a second construction fails with a descriptive exception.
Results run(const Config &config);

} // namespace lmcell
