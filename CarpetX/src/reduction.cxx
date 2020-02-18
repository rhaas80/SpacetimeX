#include "driver.hxx"
#include "reduction.hxx"
#include "schedule.hxx"

#include <cctk_Parameters.h>

#include <AMReX_MultiFabUtil.H>
#include <AMReX_Orientation.H>

#include <cstdlib>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace CarpetX {
using namespace amrex;
using namespace std;

template <typename T, int D> MPI_Datatype reduction_mpi_datatype() {
  static MPI_Datatype datatype = MPI_DATATYPE_NULL;
  if (datatype == MPI_DATATYPE_NULL) {
    MPI_Type_contiguous(sizeof(reduction<T, D>) / sizeof(T),
                        mpi_datatype<T>::value, &datatype);
    char name[MPI_MAX_OBJECT_NAME];
    int namelen;
    MPI_Type_get_name(mpi_datatype<T>::value, name, &namelen);
    ostringstream buf;
    buf << "reduction<" << name << "," << D << ">";
    string newname = buf.str();
    MPI_Type_set_name(datatype, newname.c_str());
    MPI_Type_commit(&datatype);
  }
  return datatype;
}

namespace {
template <typename T>
void mpi_reduce_typed(const void *restrict x0, void *restrict y0,
                      const int *restrict length) {
  const T *restrict x = static_cast<const T *>(x0);
  T *restrict y = static_cast<T *>(y0);
#pragma omp simd
  for (int i = 0; i < *length; ++i)
    y[i] += x[i];
}

void mpi_reduce(void *restrict x, void *restrict y, int *restrict length,
                MPI_Datatype *restrict datatype) {
  // Analyze MPI datatype
  int num_integers, num_addresses, num_datatypes, combiner;
  MPI_Type_get_envelope(*datatype, &num_integers, &num_addresses,
                        &num_datatypes, &combiner);
  assert(combiner == MPI_COMBINER_CONTIGUOUS);
  assert(num_integers == 1);
  assert(num_addresses = 1);
  assert(num_datatypes == 1);
  vector<int> integers(num_integers);
  vector<MPI_Aint> addresses(num_addresses);
  vector<MPI_Datatype> datatypes(num_datatypes);
  MPI_Type_get_contents(*datatype, num_integers, num_addresses, num_datatypes,
                        integers.data(), addresses.data(), datatypes.data());
  MPI_Datatype inner_datatype = datatypes.at(0);
  if (inner_datatype == MPI_FLOAT)
    return mpi_reduce_typed<reduction<float, dim> >(x, y, length);
  if (inner_datatype == MPI_DOUBLE)
    return mpi_reduce_typed<reduction<double, dim> >(x, y, length);
  if (inner_datatype == MPI_LONG_DOUBLE)
    return mpi_reduce_typed<reduction<long double, dim> >(x, y, length);
  abort();
}
} // namespace

MPI_Op reduction_mpi_op() {
  static MPI_Op op = MPI_OP_NULL;
  if (op == MPI_OP_NULL)
    MPI_Op_create(mpi_reduce, 1 /*commutes*/, &op);
  return op;
}

////////////////////////////////////////////////////////////////////////////////

namespace {
template <typename T>
reduction<T, dim> reduce_array(const Array4<const T> &restrict vars,
                               const int n, const array<int, dim> &imin,
                               const array<int, dim> &imax,
                               const Array4<const int> *restrict const finemask,
                               const vect<T, dim> &x0, const vect<T, dim> &dx) {
  CCTK_REAL dV = 1.0;
  for (int d = 0; d < dim; ++d)
    dV *= dx[d];
  reduction<T, dim> red;
  for (int k = imin[2]; k < imax[2]; ++k) {
    for (int j = imin[1]; j < imax[1]; ++j) {
      // #pragma omp simd reduction(red : reduction_CCTK_REAL)
      for (int i = imin[0]; i < imax[0]; ++i) {
        const bool is_masked = finemask && (*finemask)(i, j, k);
        if (!is_masked) {
          const vect<T, dim> x{x0[0] + i * dx[0], x0[1] + j * dx[1],
                               x0[2] + k * dx[2]};
          red += reduction<T, dim>(x, dV, vars(i, j, k, n));
        }
      }
    }
  }
  return red;
}
} // namespace

reduction<CCTK_REAL, dim> reduce(int gi, int vi, int tl) {
  DECLARE_CCTK_PARAMETERS;

  cGroup group;
  int ierr = CCTK_GroupData(gi, &group);
  assert(!ierr);
  assert(group.grouptype == CCTK_GF);

  reduction<CCTK_REAL, dim> red;
  for (auto &restrict leveldata : ghext->leveldata) {
    const auto &restrict groupdata = *leveldata.groupdata.at(gi);
    const MultiFab &mfab = *groupdata.mfab.at(tl);
    unique_ptr<iMultiFab> finemask_imfab;

    warn_if_invalid(leveldata, groupdata, vi, tl, make_valid_int(),
                    [] { return "Before reduction"; });

    const auto &restrict geom = ghext->amrcore->Geom(leveldata.level);
    const CCTK_REAL *restrict const x01 = geom.ProbLo();
    const CCTK_REAL *restrict const dx1 = geom.CellSize();
    const vect<CCTK_REAL, dim> x0{x01[0], x01[1], x01[2]};
    const vect<CCTK_REAL, dim> dx{dx1[0], dx1[1], dx1[2]};

    const int fine_level = leveldata.level + 1;
    if (fine_level < int(ghext->leveldata.size())) {
      const auto &restrict fine_leveldata = ghext->leveldata.at(fine_level);
      const auto &restrict fine_groupdata = *fine_leveldata.groupdata.at(gi);
      const MultiFab &fine_mfab = *fine_groupdata.mfab.at(tl);

      const IntVect reffact{2, 2, 2};
      finemask_imfab = make_unique<iMultiFab>(
          makeFineMask(mfab, fine_mfab.boxArray(), reffact));
    }

    auto mfitinfo = MFItInfo().SetDynamic(true).EnableTiling(
        {max_tile_size_x, max_tile_size_y, max_tile_size_z});
#pragma omp parallel reduction(reduction : red)
    for (MFIter mfi(mfab, mfitinfo); mfi.isValid(); ++mfi) {
      const Box &bx = mfi.tilebox(); // current region (without ghosts)
      const array<int, dim> imin{bx.smallEnd(0), bx.smallEnd(1),
                                 bx.smallEnd(2)};
      const array<int, dim> imax{bx.bigEnd(0) + 1, bx.bigEnd(1) + 1,
                                 bx.bigEnd(2) + 1};

      const Array4<const CCTK_REAL> &vars = mfab.array(mfi);

      unique_ptr<Array4<const int> > finemask;
      if (finemask_imfab)
        finemask = make_unique<Array4<const int> >(finemask_imfab->array(mfi));

      red += reduce_array(vars, vi, imin, imax, finemask.get(), x0, dx);
    }
  }

  MPI_Datatype datatype = reduction_mpi_datatype<CCTK_REAL, dim>();
  MPI_Op op = reduction_mpi_op();
  MPI_Allreduce(MPI_IN_PLACE, &red, 1, datatype, op, MPI_COMM_WORLD);

  return red;
}
} // namespace CarpetX
