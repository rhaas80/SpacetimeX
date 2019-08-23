#include "driver.hxx"
#include "schedule.hxx"
#include "timer.hxx"
#include "prolongate_3d_rf2.hxx"
#include "prolongate_3d_cc_rf2.hxx"

#include <cctk.h>
#include <cctk_Arguments.h>
#include <cctk_Parameters.h>
#include <cctk_Schedule.h>
#include <cctki_GHExtensions.h>
#include <cctki_ScheduleBindings.h>
#include <cctki_WarnLevel.h>

#include <AMReX.H>
#include <AMReX_FillPatchUtil.H>
#include <AMReX_Interpolater.H>
#include <AMReX_MultiFabUtil.H>
#include <AMReX_Orientation.H>
#include <AMReX_PhysBCFunct.H>

#include <omp.h>
#include <mpi.h>

#include <sys/time.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace AMReX {
using namespace amrex;
using namespace std;

// Value for undefined cctkGH entries
// Note: Don't use a negative value, which tends to leave bugs undetected. Large
// positive values often lead to segfault, exposing bugs.
constexpr int undefined = 666;

vector<cGH> thread_local_cctkGH;
// Tile boxes, should be part of cGH
struct TileBox {
  array<int, dim> tile_min;
  array<int, dim> tile_max;
};
vector<TileBox> thread_local_tilebox;

void Restrict(int level);

namespace {
// Convert a (direction, face) pair to an AMReX Orientation
Orientation orient(int d, int f) {
  return Orientation(d, Orientation::Side(f));
}
} // namespace

////////////////////////////////////////////////////////////////////////////////

// Set up a GridDesc
} // namespace AMReX
namespace Loop {

GridDescBase::GridDescBase() {}

GridDescBase::GridDescBase(const cGH *restrict cctkGH) {
  for (int d = 0; d < dim; ++d)
    gsh[d] = cctkGH->cctk_gsh[d];
  for (int d = 0; d < dim; ++d) {
    lbnd[d] = cctkGH->cctk_lbnd[d];
    ubnd[d] = cctkGH->cctk_ubnd[d];
  }
  for (int d = 0; d < dim; ++d)
    lsh[d] = cctkGH->cctk_lsh[d];
  for (int d = 0; d < dim; ++d)
    ash[d] = cctkGH->cctk_ash[d];
  for (int d = 0; d < dim; ++d)
    for (int f = 0; f < 2; ++f)
      bbox[2 * d + f] = cctkGH->cctk_bbox[2 * d + f];
  for (int d = 0; d < dim; ++d)
    nghostzones[d] = cctkGH->cctk_nghostzones[d];

  // Check whether we are in local mode
  assert(cctkGH->cctk_bbox[0] != AMReX::undefined);
  int thread_num = omp_get_thread_num();
  if (omp_in_parallel()) {
    const cGH *restrict threadGH = &AMReX::thread_local_cctkGH.at(thread_num);
    // Check whether this is the correct cGH structure
    assert(cctkGH == threadGH);
  }

  const AMReX::TileBox &restrict tilebox =
      AMReX::thread_local_tilebox.at(thread_num);
  for (int d = 0; d < dim; ++d) {
    tmin[d] = tilebox.tile_min[d];
    tmax[d] = tilebox.tile_max[d];
  }

  for (int d = 0; d < dim; ++d) {
    dx[d] = cctkGH->cctk_delta_space[d] / cctkGH->cctk_levfac[d];
    x0[d] = cctkGH->cctk_origin_space[d] +
            dx[d] * cctkGH->cctk_levoff[d] / cctkGH->cctk_levoffdenom[d];
  }
}

} // namespace Loop
namespace AMReX {

GridDesc::GridDesc(const GHExt::LevelData &leveldata, const MFIter &mfi) {
  const Box &fbx = mfi.fabbox();   // allocated array
  const Box &vbx = mfi.validbox(); // interior region (without ghosts)
  // const Box &bx = mfi.tilebox();       // current region (without ghosts)
  const Box &gbx = mfi.growntilebox(); // current region (with ghosts)
  const Box &domain = ghext->amrcore->Geom(leveldata.level).Domain();

  // The number of ghostzones in each direction
  for (int d = 0; d < dim; ++d)
    nghostzones[d] = mfi.theFabArrayBase().nGrowVect()[d];

  // Global shape
  for (int d = 0; d < dim; ++d)
    gsh[d] =
        domain[orient(d, 1)] + 1 - domain[orient(d, 0)] + 2 * nghostzones[d];

  // Local shape
  for (int d = 0; d < dim; ++d)
    lsh[d] = fbx[orient(d, 1)] - fbx[orient(d, 0)] + 1;

  // Allocated shape
  for (int d = 0; d < dim; ++d)
    ash[d] = fbx[orient(d, 1)] - fbx[orient(d, 0)] + 1;

  // Local extent
  for (int d = 0; d < dim; ++d) {
    lbnd[d] = fbx[orient(d, 0)] + nghostzones[d];
    ubnd[d] = fbx[orient(d, 1)] + nghostzones[d];
  }

  // Boundaries
  for (int d = 0; d < dim; ++d)
    for (int f = 0; f < 2; ++f)
      bbox[2 * d + f] = vbx[orient(d, f)] == domain[orient(d, f)];

  // Thread tile box
  for (int d = 0; d < dim; ++d) {
    tmin[d] = gbx[orient(d, 0)] - fbx[orient(d, 0)];
    tmax[d] = gbx[orient(d, 1)] + 1 - fbx[orient(d, 0)];
  }

  const Geometry &geom = ghext->amrcore->Geom(0);
  const CCTK_REAL *restrict const global_x0 = geom.ProbLo();
  const CCTK_REAL *restrict const global_dx = geom.CellSize();
  for (int d = 0; d < dim; ++d) {
    const int levfac = 1 << leveldata.level;
    const int levoff = 1 - 2 * nghostzones[d];
    const int levoffdenom = 2;
    const CCTK_REAL origin_space = global_x0[d];
    const CCTK_REAL delta_space = global_dx[d];
    dx[d] = delta_space / levfac;
    x0[d] = origin_space + dx[d] * levoff / levoffdenom;
  }

  // Check constraints
  for (int d = 0; d < dim; ++d) {
    // Domain size
    assert(gsh[d] >= 0);

    // Local size
    assert(lbnd[d] >= 0);
    assert(lsh[d] >= 0);
    assert(lbnd[d] + lsh[d] <= gsh[d]);
    assert(ubnd[d] == lbnd[d] + lsh[d] - 1);

    // Internal representation
    assert(ash[d] >= 0);
    assert(ash[d] >= lsh[d]);

    // Ghost zones
    assert(nghostzones[d] >= 0);
    assert(2 * nghostzones[d] <= lsh[d]);

    // Tiles
    assert(tmin[d] >= 0);
    assert(tmax[d] >= tmin[d]);
    assert(tmax[d] <= lsh[d]);
  }
}

GridPtrDesc::GridPtrDesc(const GHExt::LevelData &leveldata, const MFIter &mfi)
    : GridDesc(leveldata, mfi) {
  const Box &fbx = mfi.fabbox(); // allocated array
  cactus_offset = lbound(fbx);
}

////////////////////////////////////////////////////////////////////////////////

// Create a new cGH, copying those data that are set by the flesh, and
// allocating space for these data that are set per thread by the driver
void clone_cctkGH(cGH *restrict cctkGH, const cGH *restrict sourceGH) {
  // Copy all fields by default
  *cctkGH = *sourceGH;
  // Allocate most fields anew
  cctkGH->cctk_gsh = new int[dim];
  cctkGH->cctk_lsh = new int[dim];
  cctkGH->cctk_lbnd = new int[dim];
  cctkGH->cctk_ubnd = new int[dim];
  cctkGH->cctk_ash = new int[dim];
  cctkGH->cctk_to = new int[dim];
  cctkGH->cctk_from = new int[dim];
  cctkGH->cctk_delta_space = new CCTK_REAL[dim];
  cctkGH->cctk_origin_space = new CCTK_REAL[dim];
  cctkGH->cctk_bbox = new int[2 * dim];
  cctkGH->cctk_levfac = new int[dim];
  cctkGH->cctk_levoff = new int[dim];
  cctkGH->cctk_levoffdenom = new int[dim];
  cctkGH->cctk_nghostzones = new int[dim];
  const int numvars = CCTK_NumVars();
  cctkGH->data = new void **[numvars];
  for (int vi = 0; vi < numvars; ++vi)
    cctkGH->data[vi] = new void *[CCTK_DeclaredTimeLevelsVI(vi)];
}

// Initialize cctkGH entries
void setup_cctkGH(cGH *restrict cctkGH) {
  DECLARE_CCTK_PARAMETERS;

  // Grid function alignment
  // TODO: Check whether AMReX guarantees a particular alignment
  cctkGH->cctk_alignment = 1;
  cctkGH->cctk_alignment_offset = 0;

  // The refinement factor in time over the top level (coarsest) grid
  cctkGH->cctk_timefac = 1; // no subcycling

  // The convergence level (numbered from zero upwards)
  cctkGH->cctk_convlevel = 0; // no convergence tests

  // Initialize grid spacing
  const Geometry &geom = ghext->amrcore->Geom(0);
  const CCTK_REAL *restrict x0 = geom.ProbLo();
  const CCTK_REAL *restrict dx = geom.CellSize();

  for (int d = 0; d < dim; ++d) {
    cctkGH->cctk_origin_space[d] = x0[d];
    cctkGH->cctk_delta_space[d] = dx[d];
  }

  // Initialize time stepping
  // CCTK_REAL mindx = 1.0 / 0.0;
  // const int numlevels = ghext->amrcore->finestLevel() + 1;
  // for (int level = 0; level < numlevels; ++level) {
  //   const Geometry &geom = ghext->amrcore->Geom(level);
  //   const CCTK_REAL *restrict dx = geom.CellSize();
  //   for (int d = 0; d < dim; ++d)
  //     mindx = fmin(mindx, dx[d]);
  // }
  CCTK_REAL mindx = 1.0 / 0.0;
  for (int d = 0; d < dim; ++d)
    mindx = fmin(mindx, dx[d]);
  mindx = mindx / (1 << (max_num_levels - 1));
  cctkGH->cctk_time = 0.0;
  cctkGH->cctk_delta_time = dtfac * mindx;
}

// Update fields that carry state and change over time
void update_cctkGH(cGH *restrict cctkGH, const cGH *restrict sourceGH) {
  cctkGH->cctk_iteration = sourceGH->cctk_iteration;
  for (int d = 0; d < dim; ++d)
    cctkGH->cctk_origin_space[d] = sourceGH->cctk_origin_space[d];
  for (int d = 0; d < dim; ++d)
    cctkGH->cctk_delta_space[d] = sourceGH->cctk_delta_space[d];
  cctkGH->cctk_time = sourceGH->cctk_time;
  cctkGH->cctk_delta_time = sourceGH->cctk_delta_time;
}

// Set cctkGH entries for global mode
void enter_global_mode(cGH *restrict cctkGH) {
  DECLARE_CCTK_PARAMETERS;

  // The number of ghostzones in each direction
  // TODO: Get this from mfab (mfab.fb_ghosts)
  for (int d = 0; d < dim; ++d)
    cctkGH->cctk_nghostzones[d] = ghost_size;
}
void leave_global_mode(cGH *restrict cctkGH) {
  for (int d = 0; d < dim; ++d)
    cctkGH->cctk_nghostzones[d] = undefined;
}

// Set cctkGH entries for level mode
void enter_level_mode(cGH *restrict cctkGH,
                      const GHExt::LevelData &restrict leveldata) {
  DECLARE_CCTK_PARAMETERS;

  // Global shape
  const Box &domain = ghext->amrcore->Geom(leveldata.level).Domain();
  for (int d = 0; d < dim; ++d)
    cctkGH->cctk_gsh[d] = domain[orient(d, 1)] - domain[orient(d, 0)] + 1 +
                          2 * cctkGH->cctk_nghostzones[d];

  // The refinement factor over the top level (coarsest) grid
  for (int d = 0; d < dim; ++d)
    cctkGH->cctk_levfac[d] = 1 << leveldata.level;

  // Offset between this level's and the coarsest level's origin as multiple of
  // the grid spacing
  for (int d = 0; d < dim; ++d) {
    cctkGH->cctk_levoff[d] = 1 - 2 * cctkGH->cctk_nghostzones[d];
    cctkGH->cctk_levoffdenom[d] = 2;
  }
}
void leave_level_mode(cGH *restrict cctkGH,
                      const GHExt::LevelData &restrict leveldata) {
  for (int d = 0; d < dim; ++d)
    cctkGH->cctk_gsh[d] = undefined;
  for (int d = 0; d < dim; ++d)
    cctkGH->cctk_levfac[d] = undefined;
  for (int d = 0; d < dim; ++d)
    cctkGH->cctk_levoff[d] = undefined;
  for (int d = 0; d < dim; ++d)
    cctkGH->cctk_levoffdenom[d] = 0;
}

// Set cctkGH entries for local mode
void enter_local_mode(cGH *restrict cctkGH, TileBox &restrict tilebox,
                      const GHExt::LevelData &restrict leveldata,
                      const MFIter &mfi) {
  GridPtrDesc grid(leveldata, mfi);

  for (int d = 0; d < dim; ++d)
    cctkGH->cctk_lsh[d] = grid.lsh[d];
  for (int d = 0; d < dim; ++d)
    cctkGH->cctk_ash[d] = grid.ash[d];
  for (int d = 0; d < dim; ++d) {
    cctkGH->cctk_lbnd[d] = grid.lbnd[d];
    cctkGH->cctk_ubnd[d] = grid.ubnd[d];
  }
  for (int d = 0; d < dim; ++d)
    for (int f = 0; f < 2; ++f)
      cctkGH->cctk_bbox[2 * d + f] = grid.bbox[2 * d + f];

  for (int d = 0; d < dim; ++d) {
    tilebox.tile_min[d] = grid.tmin[d];
    tilebox.tile_max[d] = grid.tmax[d];
  }

  // Grid function pointers
  for (auto &restrict groupdata : leveldata.groupdata) {
    for (int tl = 0; tl < int(groupdata.mfab.size()); ++tl) {
      const Array4<CCTK_REAL> &vars = groupdata.mfab.at(tl)->array(mfi);
      for (int vi = 0; vi < groupdata.numvars; ++vi) {
        cctkGH->data[groupdata.firstvarindex + vi][tl] = grid.ptr(vars, vi);
      }
    }
  }

  // Check constraints
  for (int d = 0; d < dim; ++d) {
    // Domain size
    assert(cctkGH->cctk_gsh[d] >= 0);

    // Local size
    assert(cctkGH->cctk_lbnd[d] >= 0);
    assert(cctkGH->cctk_lsh[d] >= 0);
    assert(cctkGH->cctk_lbnd[d] + cctkGH->cctk_lsh[d] <= cctkGH->cctk_gsh[d]);
    assert(cctkGH->cctk_ubnd[d] ==
           cctkGH->cctk_lbnd[d] + cctkGH->cctk_lsh[d] - 1);

    // Internal representation
    assert(cctkGH->cctk_ash[d] >= 0);
    assert(cctkGH->cctk_ash[d] >= cctkGH->cctk_lsh[d]);

    // Ghost zones
    assert(cctkGH->cctk_nghostzones[d] >= 0);
    assert(2 * cctkGH->cctk_nghostzones[d] <= cctkGH->cctk_lsh[d]);

    // Tiles
    assert(tilebox.tile_min[d] >= 0);
    assert(tilebox.tile_max[d] >= tilebox.tile_min[d]);
    assert(tilebox.tile_max[d] <= cctkGH->cctk_lsh[d]);
  }
}
void leave_local_mode(cGH *restrict cctkGH, TileBox &restrict tilebox,
                      const GHExt::LevelData &restrict leveldata,
                      const MFIter &mfi) {
  for (int d = 0; d < dim; ++d)
    cctkGH->cctk_lsh[d] = undefined;
  for (int d = 0; d < dim; ++d)
    cctkGH->cctk_ash[d] = undefined;
  for (int d = 0; d < dim; ++d) {
    cctkGH->cctk_lbnd[d] = undefined;
    cctkGH->cctk_ubnd[d] = undefined;
  }
  for (int d = 0; d < dim; ++d)
    for (int f = 0; f < 2; ++f)
      cctkGH->cctk_bbox[2 * d + f] = undefined;
  for (int d = 0; d < dim; ++d) {
    tilebox.tile_min[d] = undefined;
    tilebox.tile_max[d] = undefined;
  }
  for (auto &restrict groupdata : leveldata.groupdata) {
    for (int tl = 0; tl < int(groupdata.mfab.size()); ++tl) {
      for (int vi = 0; vi < groupdata.numvars; ++vi)
        cctkGH->data[groupdata.firstvarindex + vi][tl] = nullptr;
    }
  }
}
extern "C" void AMReX_GetTileExtent(const void *restrict cctkGH_,
                                    CCTK_INT *restrict tile_min,
                                    CCTK_INT *restrict tile_max) {
  const cGH *restrict cctkGH = static_cast<const cGH *>(cctkGH_);
  // Check whether we are in local mode
  assert(cctkGH->cctk_bbox[0] != undefined);
  int thread_num = omp_get_thread_num();
  if (omp_in_parallel()) {
    const cGH *restrict threadGH = &thread_local_cctkGH.at(thread_num);
    // Check whether this is the correct cGH structure
    assert(cctkGH == threadGH);
  }

  const TileBox &restrict tilebox = thread_local_tilebox.at(thread_num);
  for (int d = 0; d < dim; ++d) {
    tile_min[d] = tilebox.tile_min[d];
    tile_max[d] = tilebox.tile_max[d];
  }
}

enum class mode_t { unknown, local, level, global, meta };

mode_t decode_mode(const cFunctionData *restrict attribute) {
  bool local_mode = attribute->local;
  bool level_mode = attribute->level;
  bool global_mode = attribute->global;
  bool meta_mode = attribute->meta;
  assert(int(local_mode) + int(level_mode) + int(global_mode) +
             int(meta_mode) <=
         1);
  if (attribute->local)
    return mode_t::local;
  if (attribute->level)
    return mode_t::level;
  if (attribute->global)
    return mode_t::global;
  if (attribute->meta)
    return mode_t::meta;
  return mode_t::local; // default
}

// Schedule initialisation
int Initialise(tFleshConfig *config) {
  static Timer timer("Initialise");
  Interval interval(timer);

  cGH *restrict const cctkGH = CCTK_SetupGH(config, 0);
  CCTKi_AddGH(config, 0, cctkGH);

  // Initialise iteration and time
  cctkGH->cctk_iteration = 0;
  cctkGH->cctk_time = *static_cast<const CCTK_REAL *>(
      CCTK_ParameterGet("cctk_initial_time", "Cactus", nullptr));

  // Initialise schedule
  CCTKi_ScheduleGHInit(cctkGH);

  // Initialise all grid extensions
  CCTKi_InitGHExtensions(cctkGH);

  // Set up cctkGH
  setup_cctkGH(cctkGH);
  enter_global_mode(cctkGH);
  int max_threads = omp_get_max_threads();
  thread_local_cctkGH.resize(max_threads);
  thread_local_tilebox.resize(max_threads);
  for (int n = 0; n < max_threads; ++n) {
    cGH *restrict threadGH = &thread_local_cctkGH.at(n);
    clone_cctkGH(threadGH, cctkGH);
    setup_cctkGH(threadGH);
    enter_global_mode(threadGH);
  }

  // Output domain information
  if (CCTK_MyProc(nullptr) == 0) {
    enter_level_mode(cctkGH, ghext->leveldata.at(0));
    const int *restrict gsh = cctkGH->cctk_gsh;
    const int *restrict nghostzones = cctkGH->cctk_nghostzones;
    CCTK_REAL x0[dim], x1[dim], dx[dim];
    for (int d = 0; d < dim; ++d) {
      dx[d] = cctkGH->cctk_delta_space[d];
      x0[d] = cctkGH->cctk_origin_space[d];
      x1[d] = x0[d] + (gsh[d] - 2 * nghostzones[d]) * dx[d];
    }
    CCTK_VINFO("Grid extent:");
    CCTK_VINFO("  gsh=[%d,%d,%d]", gsh[0], gsh[1], gsh[2]);
    CCTK_VINFO("Domain extent:");
    CCTK_VINFO("  xmin=[%g,%g,%g]", x0[0], x0[1], x0[2]);
    CCTK_VINFO("  xmax=[%g,%g,%g]", x1[0], x1[1], x1[2]);
    CCTK_VINFO("  base dx=[%g,%g,%g]", dx[0], dx[1], dx[2]);
    CCTK_VINFO("Time stepping:");
    CCTK_VINFO("  t0=%g", cctkGH->cctk_time);
    CCTK_VINFO("  dt=%g", cctkGH->cctk_delta_time);
    leave_level_mode(cctkGH, ghext->leveldata.at(0));
  }

  CCTK_Traverse(cctkGH, "CCTK_WRAGH");
  CCTK_Traverse(cctkGH, "CCTK_PARAMCHECK");
  CCTKi_FinaliseParamWarn();

  if (config->recovered) {
    // Recover
    CCTK_VINFO("Recovering from checkpoint...");

    const char *recovery_mode = *static_cast<const char *const *>(
        CCTK_ParameterGet("recovery_mode", "Cactus", nullptr));

    CCTK_Traverse(cctkGH, "CCTK_BASEGRID");

    if (!CCTK_Equals(recovery_mode, "strict")) {
      // Set up initial conditions
      CCTK_Traverse(cctkGH, "CCTK_INITIAL");
      CCTK_Traverse(cctkGH, "CCTK_POSTINITIAL");
      CCTK_Traverse(cctkGH, "CCTK_POSTPOSTINITIAL");
    }

    // Recover
    CCTK_Traverse(cctkGH, "CCTK_RECOVER_VARIABLES");
    CCTK_Traverse(cctkGH, "CCTK_POST_RECOVER_VARIABLES");

  } else {
    // Set up initial conditions
    CCTK_VINFO("Setting up initial conditions...");

    for (;;) {
      const int level = ghext->amrcore->finestLevel();
      CCTK_VINFO("Initializing level %d...", level);

      CCTK_Traverse(cctkGH, "CCTK_BASEGRID");
      CCTK_Traverse(cctkGH, "CCTK_INITIAL");
      CCTK_Traverse(cctkGH, "CCTK_POSTINITIAL");
      CCTK_Traverse(cctkGH, "CCTK_POSTPOSTINITIAL");

      CCTK_VINFO("Regridding...");
      const int old_numlevels = ghext->amrcore->finestLevel() + 1;
      {
        static Timer timer("InitialiseRegrid");
        Interval interval(timer);
        CreateRefinedGrid(level + 1);
      }
      const int new_numlevels = ghext->amrcore->finestLevel() + 1;
      assert(new_numlevels == old_numlevels ||
             new_numlevels == old_numlevels + 1);
      double pts0 = ghext->leveldata.at(0).mfab0->boxArray().d_numPts();
      for (const auto &leveldata : ghext->leveldata) {
        int sz = leveldata.mfab0->size();
        double pts = leveldata.mfab0->boxArray().d_numPts();
        CCTK_VINFO("  level %d: %d boxes, %.0f cells (%.4g%%)", leveldata.level,
                   sz, pts,
                   100 * pts / (pow(2.0, dim * leveldata.level) * pts0));
      }

      // Did we create a new level?
      const bool did_create_new_level = new_numlevels > old_numlevels;
      if (!did_create_new_level)
        break;

      CCTK_Traverse(cctkGH, "CCTK_POSTREGRIDINITIAL");
    }
  }
  CCTK_VINFO("Initialized %d levels", int(ghext->leveldata.size()));

  // Restrict
  for (int level = int(ghext->leveldata.size()) - 2; level >= 0; --level)
    Restrict(level);
  CCTK_Traverse(cctkGH, "CCTK_POSTRESTRICT");

  // Checkpoint, analysis, output
  CCTK_Traverse(cctkGH, "CCTK_POSTSTEP");
  CCTK_Traverse(cctkGH, "CCTK_CPINITIAL");
  CCTK_Traverse(cctkGH, "CCTK_ANALYSIS");
  CCTK_OutputGH(cctkGH);

  return 0;
}

bool EvolutionIsDone(cGH *restrict const cctkGH) {
  DECLARE_CCTK_PARAMETERS;

  static timeval starttime = {0, 0};
  // On the first time through, get the start time
  if (starttime.tv_sec == 0 && starttime.tv_usec == 0)
    gettimeofday(&starttime, nullptr);

  if (terminate_next || CCTK_TerminationReached(cctkGH))
    return true;

  if (CCTK_Equals(terminate, "never"))
    return false;

  bool max_iteration_reached = cctkGH->cctk_iteration >= cctk_itlast;

  bool max_simulation_time_reached = cctk_initial_time < cctk_final_time
                                         ? cctkGH->cctk_time >= cctk_final_time
                                         : cctkGH->cctk_time <= cctk_final_time;

  // Get the elapsed runtime in minutes and compare with max_runtime
  timeval currenttime;
  gettimeofday(&currenttime, NULL);
  bool max_runtime_reached =
      CCTK_REAL(currenttime.tv_sec - starttime.tv_sec) / 60 >= max_runtime;

  if (CCTK_Equals(terminate, "iteration"))
    return max_iteration_reached;
  if (CCTK_Equals(terminate, "time"))
    return max_simulation_time_reached;
  if (CCTK_Equals(terminate, "runtime"))
    return max_runtime_reached;
  if (CCTK_Equals(terminate, "any"))
    return max_iteration_reached || max_simulation_time_reached ||
           max_runtime_reached;
  if (CCTK_Equals(terminate, "all"))
    return max_iteration_reached && max_simulation_time_reached &&
           max_runtime_reached;
  if (CCTK_Equals(terminate, "either"))
    return max_iteration_reached || max_simulation_time_reached;
  if (CCTK_Equals(terminate, "both"))
    return max_iteration_reached && max_simulation_time_reached;

  assert(0);
}

void CycleTimelevels(cGH *restrict const cctkGH) {
  DECLARE_CCTK_PARAMETERS;

  cctkGH->cctk_iteration += 1;
  cctkGH->cctk_time += cctkGH->cctk_delta_time;

  for (auto &restrict leveldata : ghext->leveldata) {
    for (auto &restrict groupdata : leveldata.groupdata) {
      const int ntls = groupdata.mfab.size();
      if (ntls > 1) {
        unique_ptr<MultiFab> tmp = move(groupdata.mfab.at(ntls - 1));
        for (int tl = ntls - 1; tl > 0; --tl)
          groupdata.mfab.at(tl) = move(groupdata.mfab.at(tl - 1));
        groupdata.mfab.at(0) = move(tmp);

        if (poison_undefined_values) {
          // Set grid functions to nan
          const int tl = 0;
          auto mfitinfo = MFItInfo().SetDynamic(true).EnableTiling(
              {max_tile_size_x, max_tile_size_y, max_tile_size_z});
#pragma omp parallel
          for (MFIter mfi(*leveldata.mfab0, mfitinfo); mfi.isValid(); ++mfi) {
            GridPtrDesc grid(leveldata, mfi);
            const Array4<CCTK_REAL> &vars = groupdata.mfab.at(tl)->array(mfi);
            for (int vi = 0; vi < groupdata.numvars; ++vi) {
              CCTK_REAL *restrict const ptr = grid.ptr(vars, vi);
              grid.loop_all(groupdata.indextype, [&](const Loop::PointDesc &p) {
                ptr[p.idx] = 0.0 / 0.0;
              });
            }
          }
        }
      }
    }
  }
}

// Schedule evolution
int Evolve(tFleshConfig *config) {
  DECLARE_CCTK_PARAMETERS;

  static Timer timer("Evolve");
  Interval interval(timer);

  assert(config);
  cGH *restrict const cctkGH = config->GH[0];
  assert(cctkGH);

  CCTK_VINFO("Starting evolution...");

  while (!EvolutionIsDone(cctkGH)) {

    if (regrid_every > 0 && cctkGH->cctk_iteration % regrid_every == 0 &&
        ghext->amrcore->maxLevel() > 0) {
      CCTK_VINFO("Regridding...");
      CCTK_REAL time = 0.0; // dummy time
      const int old_numlevels = ghext->amrcore->finestLevel() + 1;
      {
        static Timer timer("EvolveRegrid");
        Interval interval(timer);
        ghext->amrcore->regrid(0, time);
      }
      const int new_numlevels = ghext->amrcore->finestLevel() + 1;
      CCTK_VINFO("  old levels %d, new levels %d", old_numlevels,
                 new_numlevels);
      double pts0 = ghext->leveldata.at(0).mfab0->boxArray().d_numPts();
      for (const auto &leveldata : ghext->leveldata) {
        int sz = leveldata.mfab0->size();
        double pts = leveldata.mfab0->boxArray().d_numPts();
        CCTK_VINFO("  level %d: %d boxes, %.0f cells (%.4g%%)", leveldata.level,
                   sz, pts,
                   100 * pts / (pow(2.0, dim * leveldata.level) * pts0));
      }

      CCTK_Traverse(cctkGH, "CCTK_BASEGRID");
      CCTK_Traverse(cctkGH, "CCTK_POSTREGRID");
    }

    CycleTimelevels(cctkGH);

    CCTK_Traverse(cctkGH, "CCTK_PRESTEP");
    CCTK_Traverse(cctkGH, "CCTK_EVOL");

    // Restrict
    for (int level = (ghext->leveldata.size()) - 2; level >= 0; --level)
      Restrict(level);
    CCTK_Traverse(cctkGH, "CCTK_POSTRESTRICT");

    CCTK_Traverse(cctkGH, "CCTK_POSTSTEP");
    CCTK_Traverse(cctkGH, "CCTK_CHECKPOINT");
    CCTK_Traverse(cctkGH, "CCTK_ANALYSIS");
    CCTK_OutputGH(cctkGH);
  }

  return 0;
}

// Schedule shutdown
int Shutdown(tFleshConfig *config) {
  assert(config);
  cGH *restrict const cctkGH = config->GH[0];
  assert(cctkGH);

  static Timer timer("Shutdown");
  Interval interval(timer);

  CCTK_VINFO("Shutting down...");

  CCTK_Traverse(cctkGH, "CCTK_TERMINATE");
  CCTK_Traverse(cctkGH, "CCTK_SHUTDOWN");

  return 0;
}

// Call a scheduled function
int CallFunction(void *function, cFunctionData *restrict attribute,
                 void *data) {
  DECLARE_CCTK_PARAMETERS;

  assert(function);
  assert(attribute);
  assert(data);

  cGH *restrict const cctkGH = static_cast<cGH *>(data);

  if (verbose)
    CCTK_VINFO("CallFunction iteration %d %s: %s::%s", cctkGH->cctk_iteration,
               attribute->where, attribute->thorn, attribute->routine);

  const mode_t mode = decode_mode(attribute);
  switch (mode) {
  case mode_t::local: {
    // Call function once per tile
#pragma omp parallel
    {
      int thread_num = omp_get_thread_num();
      cGH *restrict threadGH = &thread_local_cctkGH.at(thread_num);
      update_cctkGH(threadGH, cctkGH);
      TileBox &restrict thread_tilebox = thread_local_tilebox.at(thread_num);

      // Loop over all levels
      // TODO: parallelize this loop
      for (auto &restrict leveldata : ghext->leveldata) {
        enter_level_mode(threadGH, leveldata);
        auto mfitinfo = MFItInfo().SetDynamic(true).EnableTiling(
            {max_tile_size_x, max_tile_size_y, max_tile_size_z});
        for (MFIter mfi(*leveldata.mfab0, mfitinfo); mfi.isValid(); ++mfi) {
          enter_local_mode(threadGH, thread_tilebox, leveldata, mfi);
          CCTK_CallFunction(function, attribute, threadGH);
          leave_local_mode(threadGH, thread_tilebox, leveldata, mfi);
        }
        leave_level_mode(threadGH, leveldata);
      }
    }
    break;
  }
  case mode_t::meta:
  case mode_t::global:
  case mode_t::level: {
    // Call function just once
    // Note: meta mode scheduling must continue to work even after we
    // shut down ourselves!
    CCTK_CallFunction(function, attribute, cctkGH);
    break;
  }

  default:
    assert(0);
  }

  int didsync = 0;
  return didsync;
}

int SyncGroupsByDirI(const cGH *restrict cctkGH, int numgroups,
                     const int *groups, const int *directions) {
  DECLARE_CCTK_PARAMETERS;

  static Timer timer("Sync");
  Interval interval(timer);

  assert(cctkGH);
  assert(numgroups >= 0);
  assert(groups);

  if (verbose) {
    ostringstream buf;
    for (int n = 0; n < numgroups; ++n) {
      if (n != 0)
        buf << ", ";
      buf << unique_ptr<char>(CCTK_GroupName(groups[n])).get();
    }
    CCTK_VINFO("SyncGroups %s", buf.str().c_str());
  }

  for (auto &restrict leveldata : ghext->leveldata) {
    for (int n = 0; n < numgroups; ++n) {
      int gi = groups[n];
      auto &restrict groupdata = leveldata.groupdata.at(gi);
      // We always sync all directions.
      // If there is more than one time level, then we don't sync the
      // oldest.
#warning "TODO: during evolution, sync only one time level"
      int ntls = groupdata.mfab.size();
      int sync_tl = ntls > 1 ? ntls - 1 : ntls;

      if (leveldata.level == 0) {
        // Coarsest level: Copy from adjacent boxes on same level

        for (int tl = 0; tl < sync_tl; ++tl)
          groupdata.mfab.at(tl)->FillBoundary(
              ghext->amrcore->Geom(leveldata.level).periodicity());

      } else {
        // Refined level: Prolongate boundaries from next coarser
        // level, and copy from adjacent boxes on same level

        const int level = leveldata.level;
        auto &restrict coarsegroupdata =
            ghext->leveldata.at(level - 1).groupdata.at(gi);
        assert(coarsegroupdata.numvars == groupdata.numvars);
        PhysBCFunctNoOp cphysbc;
        PhysBCFunctNoOp fphysbc;
        const IntVect reffact{2, 2, 2};
        // boundary conditions
        const BCRec bcrec(periodic_x ? BCType::int_dir : BCType::reflect_odd,
                          periodic_y ? BCType::int_dir : BCType::reflect_odd,
                          periodic_z ? BCType::int_dir : BCType::reflect_odd,
                          periodic_x ? BCType::int_dir : BCType::reflect_odd,
                          periodic_y ? BCType::int_dir : BCType::reflect_odd,
                          periodic_z ? BCType::int_dir : BCType::reflect_odd);
        const Vector<BCRec> bcs(groupdata.numvars, bcrec);
        for (int tl = 0; tl < sync_tl; ++tl)
          FillPatchTwoLevels(
              *groupdata.mfab.at(tl), 0.0, {&*coarsegroupdata.mfab.at(tl)},
              {0.0}, {&*groupdata.mfab.at(tl)}, {0.0}, 0, 0, groupdata.numvars,
              ghext->amrcore->Geom(level - 1), ghext->amrcore->Geom(level),
              cphysbc, 0, fphysbc, 0, reffact, &prolongate3d_cc_rf2_o4, bcs, 0);
      }
    }
  }

  return numgroups; // number of groups synchronized
}

void Restrict(int level) {
  DECLARE_CCTK_PARAMETERS;

  static Timer timer("Restrict");
  Interval interval(timer);

  const int gi_regrid_error = CCTK_GroupIndex("AMReX::regrid_error");
  assert(gi_regrid_error >= 0);
  const int gi_refinement_level = CCTK_GroupIndex("AMReX::refinement_level");
  assert(gi_refinement_level >= 0);

  auto &leveldata = ghext->leveldata.at(level);
  const auto &fine_leveldata = ghext->leveldata.at(level + 1);
  for (int gi = 0; gi < int(leveldata.groupdata.size()); ++gi) {
    // Don't restrict the regridding error nor the refinement level
    if (gi == gi_regrid_error || gi == gi_refinement_level)
      continue;
    auto &groupdata = leveldata.groupdata.at(gi);
    const auto &fine_groupdata = fine_leveldata.groupdata.at(gi);
    // If there is more than one time level, then we don't restrict
    // the oldest.
#warning "TODO: during evolution, restrict only one time level"
    int ntls = groupdata.mfab.size();
    int restrict_tl = ntls > 1 ? ntls - 1 : ntls;
    const IntVect reffact{2, 2, 2};
    for (int tl = 0; tl < restrict_tl; ++tl) {

      if (poison_undefined_values) {
        MultiFab &mfab = *groupdata.mfab.at(tl);
        const MultiFab &fine_mfab = *fine_groupdata.mfab.at(tl);
        const IntVect reffact{2, 2, 2};
        const iMultiFab finemask =
            makeFineMask(mfab, fine_mfab.boxArray(), reffact);
        auto mfitinfo = MFItInfo().SetDynamic(true).EnableTiling(
            {max_tile_size_x, max_tile_size_y, max_tile_size_z});
#pragma omp parallel
        for (MFIter mfi(*leveldata.mfab0, mfitinfo); mfi.isValid(); ++mfi) {
          GridPtrDesc grid(leveldata, mfi);
          const Array4<const int> &mask = finemask.array(mfi);
          const Array4<CCTK_REAL> &vars = mfab.array(mfi);
          for (int vi = 0; vi < groupdata.numvars; ++vi) {
            CCTK_REAL *restrict const ptr = grid.ptr(vars, vi);

#if 0
#warning                                                                       \
    "TODO: Remove this loop; it only checks AMReX's internal correctness, and this is not a good way for doing so"
            // Poison points that will be restricted
            grid.loop_all(groupdata.indextype, [&](const Loop::PointDesc &p) {
              const int i = grid.cactus_offset.x + p.i;
              const int j = grid.cactus_offset.y + p.j;
              const int k = grid.cactus_offset.z + p.k;
              bool is_restricted = true;
              // For cell centred indices (indextype=1), check the
              // mask directly. For vertex centred indices
              // (indextype=0), check the two neighbouring cells. We
              // assume restriction is possible if either of the cells
              // is unmasked. At the boundary of the mask grid
              // function, be conservative and don't poison.
              for (int c = groupdata.indextype[2] - 1; c < 1; ++c)
                for (int b = groupdata.indextype[1] - 1; b < 1; ++b)
                  for (int a = groupdata.indextype[0] - 1; a < 1; ++a)
                    if (i + a >= mask.begin.x && i + a < mask.end.x &&
                        j + b >= mask.begin.y && j + b < mask.end.y &&
                        k + c >= mask.begin.z && k + c < mask.end.z)
                      is_restricted &= mask(i + a, j + b, k + c);
                    else
                      is_restricted = false; // be conservative
              if (is_restricted)
                ptr[p.idx] = 0.0 / 0.0;
            });
#endif

            // Poison boundaries
            grid.loop_bnd(groupdata.indextype, [&](const Loop::PointDesc &p) {
                ptr[p.idx] = 0.0 / 0.0;
            });
          }
        }
      }

      if (groupdata.indextype == array<int, dim>{0, 0, 0})
        amrex::average_down_nodal(*fine_groupdata.mfab.at(tl),
                                  *groupdata.mfab.at(tl), reffact);
      else if (groupdata.indextype == array<int, dim>{1, 0, 0} ||
               groupdata.indextype == array<int, dim>{0, 1, 0} ||
               groupdata.indextype == array<int, dim>{0, 0, 1})
        amrex::average_down_edges(*fine_groupdata.mfab.at(tl),
                                  *groupdata.mfab.at(tl), reffact);
      else if (groupdata.indextype == array<int, dim>{1, 1, 0} ||
               groupdata.indextype == array<int, dim>{1, 0, 1} ||
               groupdata.indextype == array<int, dim>{0, 1, 1})
        amrex::average_down_faces(*fine_groupdata.mfab.at(tl),
                                  *groupdata.mfab.at(tl), reffact);
      else if (groupdata.indextype == array<int, dim>{1, 1, 1})
        amrex::average_down(*fine_groupdata.mfab.at(tl), *groupdata.mfab.at(tl),
                            0, groupdata.numvars, reffact);
      else
        assert(0);
    }
  }
}

} // namespace AMReX
