#ifndef LOOP_HXX
#define LOOP_HXX

#include <cctk.h>
#undef copysign
#undef fpclassify
#undef isfinite
#undef isinf
#undef isnan
#undef isnormal
#undef signbit

#include <algorithm>
#include <array>
#include <functional>
#include <ostream>
#include <string>
#include <tuple>

namespace Loop {
using namespace std;

constexpr int dim = 3;

////////////////////////////////////////////////////////////////////////////////

template <typename T, int D> struct vect {
  array<T, D> elts;

  vect() {
    for (int d = 0; d < D; ++d)
      elts[d] = 0;
  }

  vect(const array<T, D> &arr) : elts(arr) {}

  static vect unit(int dir) {
    vect r;
    for (int d = 0; d < D; ++d)
      r.elts[d] = d == dir;
    return r;
  }

  const T &operator[](int d) const { return elts[d]; }
  T &operator[](int d) { return elts[d]; }

  friend vect operator+(const vect &x) {
    vect r;
    for (int d = 0; d < D; ++d)
      r.elts[d] = +x.elts[d];
    return r;
  }
  friend vect operator-(const vect &x) {
    vect r;
    for (int d = 0; d < D; ++d)
      r.elts[d] = -x.elts[d];
    return r;
  }

  friend vect operator+(const vect &x, const vect &y) {
    vect r;
    for (int d = 0; d < D; ++d)
      r.elts[d] = x.elts[d] + y.elts[d];
    return r;
  }
  friend vect operator-(const vect &x, const vect &y) {
    vect r;
    for (int d = 0; d < D; ++d)
      r.elts[d] = x.elts[d] - y.elts[d];
    return r;
  }

  // friend vect operator+(const vect &x, const array<T, D> &y) {
  //   return x + vect(y);
  // }
  // friend vect operator-(const vect &x, const array<T, D> &y) {
  //   return x - vect(y);
  // }

  // friend vect operator+(const vect &x, T a) {
  //   vect r;
  //   for (int d = 0; d < D; ++d)
  //     r.elts[d] = x.elts[d] + a;
  //   return r;
  // }
  // friend vect operator-(const vect &x, T a) {
  //   vect r;
  //   for (int d = 0; d < D; ++d)
  //     r.elts[d] = x.elts[d] - a;
  //   return r;
  // }

  friend vect operator*(T a, const vect &x) {
    vect r;
    for (int d = 0; d < D; ++d)
      r.elts[d] = a * x.elts[d];
    return r;
  }
  friend vect operator*(const vect &x, T a) {
    vect r;
    for (int d = 0; d < D; ++d)
      r.elts[d] = x.elts[d] * a;
    return r;
  }

  friend vect<bool, D> operator!(const vect &x) {
    vect<bool, D> r;
    for (int d = 0; d < dim; ++d)
      r.elts[d] = !x.elts[d];
    return r;
  }
  friend vect<bool, D> operator&&(const vect &x, const vect &y) {
    vect<bool, D> r;
    for (int d = 0; d < D; ++d)
      r.elts[d] = x.elts[d] && y.elts[d];
    return r;
  }
  friend vect<bool, D> operator||(const vect &x, const vect &y) {
    vect<bool, D> r;
    for (int d = 0; d < D; ++d)
      r.elts[d] = x.elts[d] || y.elts[d];
    return r;
  }

  friend vect<bool, D> operator==(const vect &x, const vect &y) {
    vect<bool, D> r;
    for (int d = 0; d < D; ++d)
      r.elts[d] = x.elts[d] == y.elts[d];
    return r;
  }
  friend vect<bool, D> operator!=(const vect &x, const vect &y) {
    return !(x == y);
  }
  friend vect<bool, D> operator<(const vect &x, const vect &y) {
    vect<bool, D> r;
    for (int d = 0; d < D; ++d)
      r.elts[d] = x.elts[d] < y.elts[d];
    return r;
  }
  friend vect<bool, D> operator>(const vect &x, const vect &y) { return y < x; }
  friend vect<bool, D> operator<=(const vect &x, const vect &y) {
    return !(x > y);
  }
  friend vect<bool, D> operator>=(const vect &x, const vect &y) {
    return !(x < y);
  }

  template <typename U>
  vect<U, D> ifelse(const Loop::vect<U, D> &x,
                    const Loop::vect<U, D> &y) const {
    vect<U, D> r;
    for (int d = 0; d < D; ++d)
      r.elts[d] = elts[d] ? x.elts[d] : y.elts[d];
    return r;
  }

  friend ostream &operator<<(ostream &os, const vect &x) {
    os << "[";
    for (int d = 0; d < D; ++d) {
      if (d > 0)
        os << ",";
      os << x.elts[d];
    }
    os << "]";
    return os;
  }
};

} // namespace Loop
namespace std {

template <typename T, int D> struct equal_to<Loop::vect<T, D> > {
  constexpr bool operator()(const Loop::vect<T, D> &lhs,
                            const Loop::vect<T, D> &rhs) const {
    return equal_to<array<T, D> >()(lhs.elts, rhs.elts);
  }
};

template <typename T, int D> struct less<Loop::vect<T, D> > {
  constexpr bool operator()(const Loop::vect<T, D> &lhs,
                            const Loop::vect<T, D> &rhs) const {
    return less<array<T, D> >(lhs.elts, rhs.elts);
  }
};

template <typename T, int D>
Loop::vect<T, D> max(const Loop::vect<T, D> &x, const Loop::vect<T, D> &y) {
  Loop::vect<T, D> r;
  for (int d = 0; d < D; ++d)
    r.elts[d] = max(x.elts[d], y.elts[d]);
  return r;
}

template <typename T, int D>
Loop::vect<T, D> min(const Loop::vect<T, D> &x, const Loop::vect<T, D> &y) {
  Loop::vect<T, D> r;
  for (int d = 0; d < D; ++d)
    r.elts[d] = min(x.elts[d], y.elts[d]);
  return r;
}

} // namespace std
namespace Loop {

////////////////////////////////////////////////////////////////////////////////

// TODO: remove this
template <typename T, int CI, int CJ, int CK> struct GF3D {
  static_assert(CI == 0 || CI == 1, "");
  static_assert(CJ == 0 || CJ == 1, "");
  static_assert(CK == 0 || CK == 1, "");
  typedef T value_type;
  T *restrict ptr;
  static constexpr int di = 1;
  int dj, dk;
  int ni, nj, nk;
  static constexpr array<int, dim> indextype() { return {CI, CJ, CK}; }
  inline GF3D(const cGH *restrict cctkGH, T *restrict ptr)
      : ptr(ptr), dj(di * (cctkGH->cctk_ash[0] + 1 - CI)),
        dk(dj * (cctkGH->cctk_ash[1] + 1 - CJ)),
        ni(cctkGH->cctk_lsh[0] + 1 - CI), nj(cctkGH->cctk_lsh[1] + 1 - CJ),
        nk(cctkGH->cctk_lsh[2] + 1 - CK) {}
  inline int offset(int i, int j, int k) const {
    // These index checks prevent vectorization. We thus only enable
    // them in debug mode.
#ifdef CCTK_DEBUG
    assert(i >= 0 && i < ni);
    assert(j >= 0 && j < nj);
    assert(k >= 0 && k < nk);
#endif
    return i * di + j * dj + k * dk;
  }
  inline T &restrict operator()(int i, int j, int k) const {
    return ptr[offset(i, j, k)];
  }
  inline T &restrict operator()(const vect<int, dim> &I) const {
    return ptr[offset(I[0], I[1], I[2])];
  }
};

template <typename T> struct GF3D1 {
  typedef T value_type;
  T *restrict ptr;
#ifdef CCTK_DEBUG
  array<int, dim> imin, imax;
  array<int, dim> ash;
#endif
  static constexpr int di = 1;
  int dj, dk;
  int off;
  inline GF3D1(T *restrict ptr, const array<int, dim> &imin,
               const array<int, dim> &imax, const array<int, dim> &ash)
      : ptr(ptr),
#ifdef CCTK_DEBUG
        imin(imin), imax(imax), ash(ash),
#endif
        dj(di * ash[0]), dk(dj * ash[1]),
        off(imin[0] * di + imin[1] * dj + imin[2] * dk) {
  }
  inline GF3D1(const cGH *restrict cctkGH, const array<int, dim> &indextype,
               const array<int, dim> &nghostzones, T *restrict ptr) {
    for (int d = 0; d < dim; ++d)
      assert(indextype[d] == 0 || indextype[d] == 1);
    for (int d = 0; d < dim; ++d) {
      assert(nghostzones[d] >= 0);
      assert(nghostzones[d] <= cctkGH->cctk_nghostzones[d]);
    }
    array<int, dim> imin, imax;
    for (int d = 0; d < dim; ++d) {
      imin[d] = cctkGH->cctk_nghostzones[d] - nghostzones[d];
      imax[d] = cctkGH->cctk_lsh[d] + (1 - indextype[d]) -
                (cctkGH->cctk_nghostzones[d] - nghostzones[d]);
    }
    array<int, dim> ash;
    for (int d = 0; d < dim; ++d)
      ash[d] = cctkGH->cctk_ash[d] + (1 - indextype[d]) -
               2 * (cctkGH->cctk_nghostzones[d] - nghostzones[d]);
    *this = GF3D1(ptr, imin, imax, ash);
  }
  inline int offset(int i, int j, int k) const {
    // These index checks prevent vectorization. We thus only enable
    // them in debug mode.
#ifdef CCTK_DEBUG
    assert(i >= imin[0] && i < imax[0]);
    assert(j >= imin[1] && j < imax[1]);
    assert(k >= imin[2] && k < imax[2]);
#endif
    return i * di + j * dj + k * dk - off;
  }
  inline T &restrict operator()(int i, int j, int k) const {
    return ptr[offset(i, j, k)];
  }
  inline T &restrict operator()(const vect<int, dim> &I) const {
    return ptr[offset(I[0], I[1], I[2])];
  }
};

////////////////////////////////////////////////////////////////////////////////

enum class where_t { everywhere, interior, boundary };

struct PointDesc {
  int i, j, k;
  CCTK_REAL x, y, z;
  int idx;
  static constexpr int di = 1;
  int dj, dk;
  vect<int, dim> I;
  vect<int, dim> DI(int d) const { return vect<int, dim>::unit(d); }
  friend ostream &operator<<(ostream &os, const PointDesc &p) {
    os << "PointDesc{"
       << "ijk:"
       << "{" << p.i << "," << p.j << "," << p.k << "}, "
       << "xyz:"
       << "{" << p.x << "," << p.y << "," << p.z << "}, "
       << "idx:" << p.idx << ", "
       << "dijk:"
       << "{" << p.di << "," << p.dj << "," << p.dk << "}"
       << "}";
    return os;
  }
};

struct GridDescBase {
  array<int, dim> gsh;
  array<int, dim> lbnd, ubnd;
  array<int, dim> lsh;
  array<int, dim> ash;
  array<int, 2 * dim> bbox;
  array<int, dim> nghostzones;
  array<int, dim> tmin, tmax;

  array<CCTK_REAL, dim> x0;
  array<CCTK_REAL, dim> dx;

  template <typename T, size_t N>
  static void output(ostream &os, const string &str, const array<T, N> &arr) {
    os << str << ":[";
    for (size_t n = 0; n < N; ++n) {
      if (n > 0)
        os << ",";
      os << arr[n];
    }
    os << "]";
  }
  friend ostream &operator<<(ostream &os, const GridDescBase &grid) {
    os << "GridDescBase{";
    output(os, "gsh", grid.gsh);
    output(os, ",lbnd", grid.lbnd);
    output(os, ",ubnd", grid.ubnd);
    output(os, ",lsh", grid.lsh);
    output(os, ",bbox", grid.bbox);
    output(os, ",nghostzones", grid.nghostzones);
    output(os, ",tmin", grid.tmin);
    output(os, ",tmax", grid.tmax);
    os << "}";
    return os;
  }

protected:
  GridDescBase();

public:
  GridDescBase(const cGH *cctkGH);

  // Loop over a given box
  template <int CI, int CJ, int CK, typename F>
  void loop_box(const F &f, const array<int, dim> &restrict imin,
                const array<int, dim> &restrict imax) const {
    static_assert(CI == 0 || CI == 1, "");
    static_assert(CJ == 0 || CJ == 1, "");
    static_assert(CK == 0 || CK == 1, "");

    for (int d = 0; d < dim; ++d)
      if (imin[d] >= imax[d])
        return;

    constexpr int di = 1;
    const int dj = di * (ash[0] + 1 - CI);
    const int dk = dj * (ash[1] + 1 - CJ);

    for (int k = imin[2]; k < imax[2]; ++k) {
      for (int j = imin[1]; j < imax[1]; ++j) {
#pragma omp simd
        for (int i = imin[0]; i < imax[0]; ++i) {
          CCTK_REAL x = x0[0] + (lbnd[0] + i + CCTK_REAL(CI - 1) / 2) * dx[0];
          CCTK_REAL y = x0[1] + (lbnd[1] + j + CCTK_REAL(CJ - 1) / 2) * dx[1];
          CCTK_REAL z = x0[2] + (lbnd[2] + k + CCTK_REAL(CK - 1) / 2) * dx[2];
          int idx = i * di + j * dj + k * dk;
          vect<int, dim> I{{i, j, k}};
          const PointDesc p{i, j, k, x, y, z, idx, dj, dk, I};
          f(p);
        }
      }
    }
  }

  // Loop over all points
  template <int CI, int CJ, int CK, typename F>
  void loop_all(const array<int, dim> &group_nghostzones, const F &f) const {
    constexpr array<int, dim> offset{!CI, !CJ, !CK};
    array<int, dim> imin, imax;
    for (int d = 0; d < dim; ++d) {
      int ghost_offset = nghostzones[d] - group_nghostzones[d];
      imin[d] = max(tmin[d], ghost_offset);
      imax[d] = min(tmax[d] + (tmax[d] >= lsh[d] ? offset[d] : 0),
                    lsh[d] + offset[d] - ghost_offset);
    }

    loop_box<CI, CJ, CK>(f, imin, imax);
  }

  // Loop over all interior points
  template <int CI, int CJ, int CK, typename F>
  void loop_int(const array<int, dim> &group_nghostzones, const F &f) const {
    constexpr array<int, dim> offset{!CI, !CJ, !CK};
    array<int, dim> imin, imax;
    for (int d = 0; d < dim; ++d) {
      imin[d] = max(tmin[d], nghostzones[d]);
      imax[d] = min(tmax[d] + (tmax[d] >= lsh[d] ? offset[d] : 0),
                    lsh[d] + offset[d] - nghostzones[d]);
    }

    loop_box<CI, CJ, CK>(f, imin, imax);
  }

  // Loop over all outer boundary points. This excludes ghost faces, but
  // includes ghost edges/corners on non-ghost faces.
  template <int CI, int CJ, int CK, typename F>
  void loop_bnd(const array<int, dim> &group_nghostzones, const F &f) const {
    constexpr array<int, dim> offset{!CI, !CJ, !CK};

    for (int dir = 0; dir < dim; ++dir) {
      for (int face = 0; face < 2; ++face) {
        if (bbox[2 * dir + face]) {

          array<int, dim> imin, imax;
          for (int d = 0; d < dim; ++d) {
            // by default, include interior and outer boundaries and ghosts
            int ghost_offset = nghostzones[d] - group_nghostzones[d];
            imin[d] = ghost_offset;
            imax[d] = lsh[d] + offset[d] - ghost_offset;

            // avoid covering edges and corners multiple times
            if (d < dir) {
              if (bbox[2 * d])
                imin[d] = nghostzones[d]; // only interior
              if (bbox[2 * d + 1])
                imax[d] = lsh[d] + offset[d] - nghostzones[d]; // only interior
            }
          }
          // only one face on outer boundary
          if (face == 0)
            imax[dir] = nghostzones[dir];
          else
            imin[dir] = lsh[dir] + offset[dir] - nghostzones[dir];

          for (int d = 0; d < dim; ++d) {
            imin[d] = max(tmin[d], imin[d]);
            imax[d] =
                min(tmax[d] + (tmax[d] >= lsh[d] ? offset[d] : 0), imax[d]);
          }

          loop_box<CI, CJ, CK>(f, imin, imax);
        }
      }
    }
  }

  template <int CI, int CJ, int CK, typename F>
  void loop(where_t where, const array<int, dim> &group_nghostzones,
            const F &f) const {
    switch (where) {
    case where_t::everywhere:
      return loop_all<CI, CJ, CK>(group_nghostzones, f);
    case where_t::interior:
      return loop_int<CI, CJ, CK>(group_nghostzones, f);
    case where_t::boundary:
      return loop_bnd<CI, CJ, CK>(group_nghostzones, f);
    default:
      assert(0);
    }
  }

  template <int CI, int CJ, int CK, typename F>
  void loop(where_t where, const F &f) const {
    loop<CI, CJ, CK>(where, nghostzones, f);
  }

  template <typename F>
  void loop_idx(where_t where, const array<int, dim> &indextype,
                const array<int, dim> &group_nghostzones, const F &f) const {
    switch (indextype[0] + 2 * indextype[1] + 4 * indextype[2]) {
    case 0b000:
      return loop<0, 0, 0>(where, group_nghostzones, f);
    case 0b001:
      return loop<1, 0, 0>(where, group_nghostzones, f);
    case 0b010:
      return loop<0, 1, 0>(where, group_nghostzones, f);
    case 0b011:
      return loop<1, 1, 0>(where, group_nghostzones, f);
    case 0b100:
      return loop<0, 0, 1>(where, group_nghostzones, f);
    case 0b101:
      return loop<1, 0, 1>(where, group_nghostzones, f);
    case 0b110:
      return loop<0, 1, 1>(where, group_nghostzones, f);
    case 0b111:
      return loop<1, 1, 1>(where, group_nghostzones, f);
    default:
      assert(0);
    }
  }

  template <typename F>
  void loop_idx(where_t where, const array<int, dim> &indextype,
                const F &f) const {
    loop_idx(where, indextype, nghostzones, f);
  }
};

template <typename F>
void loop_idx(const cGH *cctkGH, where_t where,
              const array<int, dim> &indextype,
              const array<int, dim> &nghostzones, const F &f) {
  GridDescBase(cctkGH).loop_idx(where, indextype, nghostzones, f);
}

template <typename F>
void loop_idx(const cGH *cctkGH, where_t where,
              const array<int, dim> &indextype, const F &f) {
  GridDescBase(cctkGH).loop_idx(where, indextype, f);
}

template <int CI, int CJ, int CK, typename F>
void loop(const cGH *cctkGH, where_t where, const F &f) {
  GridDescBase(cctkGH).loop<CI, CJ, CK>(where, f);
}

// Keep these for convenience
template <int CI, int CJ, int CK, typename F>
void loop_all(const cGH *cctkGH, const F &f) {
  loop<CI, CJ, CK>(cctkGH, where_t::everywhere, f);
}

template <int CI, int CJ, int CK, typename F>
void loop_int(const cGH *cctkGH, const F &f) {
  loop<CI, CJ, CK>(cctkGH, where_t::interior, f);
}

template <int CI, int CJ, int CK, typename F>
void loop_bnd(const cGH *cctkGH, const F &f) {
  loop<CI, CJ, CK>(cctkGH, where_t::boundary, f);
}

} // namespace Loop

#endif // #ifndef LOOP_HXX