/********************************************************************************
 * HOMMEXX 1.0: Copyright of Sandia Corporation
 * This software is released under the BSD license
 * See the file 'COPYRIGHT' in the HOMMEXX/src/share/cxx directory
 *******************************************************************************/

#ifndef HOMMEXX_CAAR_FUNCTOR_IMPL_HPP
#define HOMMEXX_CAAR_FUNCTOR_IMPL_HPP

#include "Types.hpp"
#include "Elements.hpp"
#include "ColumnOps.hpp"
#include "EquationOfState.hpp"
#include "FunctorsBuffersManager.hpp"
#include "HybridVCoord.hpp"
#include "KernelVariables.hpp"
#include "ReferenceElement.hpp"
#include "RKStageData.hpp"
#include "SimulationParams.hpp"
#include "SphereOperators.hpp"

#include "mpi/BoundaryExchange.hpp"
#include "mpi/MpiBuffersManager.hpp"
#include "utilities/SubviewUtils.hpp"
#include "utilities/ViewUtils.hpp"

#include "profiling.hpp"
#include "ErrorDefs.hpp"

#include <assert.h>

namespace Homme {

// Theta does not use tracers in caar. A fwd decl is enough here
struct Tracers;

struct CaarFunctorImpl {

  struct Buffers {
    static constexpr int num_3d_scalar_mid_buf = 12;
    static constexpr int num_3d_vector_mid_buf =  4;
    static constexpr int num_3d_scalar_int_buf =  8;
    static constexpr int num_3d_vector_int_buf =  3;

    ExecViewUnmanaged<Scalar*    [NP][NP][NUM_LEV]  >   temp;

    ExecViewUnmanaged<Scalar*    [NP][NP][NUM_LEV]  >   pnh;
    ExecViewUnmanaged<Scalar*    [NP][NP][NUM_LEV]  >   pi;
    ExecViewUnmanaged<Scalar*    [NP][NP][NUM_LEV]  >   exner;
    ExecViewUnmanaged<Scalar*    [NP][NP][NUM_LEV]  >   div_vdp;
    ExecViewUnmanaged<Scalar*    [NP][NP][NUM_LEV]  >   phi;
    ExecViewUnmanaged<Scalar*    [NP][NP][NUM_LEV]  >   omega_p;
    ExecViewUnmanaged<Scalar*    [NP][NP][NUM_LEV]  >   theta_vadv;
    ExecViewUnmanaged<Scalar*    [NP][NP][NUM_LEV]  >   vort;

    ExecViewUnmanaged<Scalar* [2][NP][NP][NUM_LEV]  >   v_vadv;
    ExecViewUnmanaged<Scalar* [2][NP][NP][NUM_LEV]  >   grad_tmp;
    ExecViewUnmanaged<Scalar* [2][NP][NP][NUM_LEV]  >   vdp;

    ExecViewUnmanaged<Scalar*    [NP][NP][NUM_LEV_P]>   dp_i;
    ExecViewUnmanaged<Scalar*    [NP][NP][NUM_LEV_P]>   vtheta_i;
    ExecViewUnmanaged<Scalar*    [NP][NP][NUM_LEV_P]>   dpnh_dp_i;
    ExecViewUnmanaged<Scalar*    [NP][NP][NUM_LEV_P]>   w_vadv;
    ExecViewUnmanaged<Scalar*    [NP][NP][NUM_LEV_P]>   phi_vadv;
    ExecViewUnmanaged<Scalar*    [NP][NP][NUM_LEV_P]>   eta_dot_dpdn;

    ExecViewUnmanaged<Scalar* [2][NP][NP][NUM_LEV_P]>   v_i;
    ExecViewUnmanaged<Scalar* [2][NP][NP][NUM_LEV_P]>   grad_phinh_i;
    ExecViewUnmanaged<Scalar* [2][NP][NP][NUM_LEV_P]>   grad_w_i;

    ExecViewUnmanaged<Scalar* [2][NP][NP][NUM_LEV]  >   v_tens;
    ExecViewUnmanaged<Scalar*    [NP][NP][NUM_LEV]  >   theta_tens;
    ExecViewUnmanaged<Scalar*    [NP][NP][NUM_LEV]  >   dp_tens;
    ExecViewUnmanaged<Scalar*    [NP][NP][NUM_LEV_P]>   w_tens;
    ExecViewUnmanaged<Scalar*    [NP][NP][NUM_LEV_P]>   phi_tens;
  };

  using deriv_type = ReferenceElement::deriv_type;

  RKStageData                 m_data;

  // The 'm_data.scale2' coeff used for the calculation of w_tens and phi_tens must be replaced
  // with 'm_data.scale1' on the surface (k=NUM_INTERFACE_LEV). To avoid if statements deep in
  // the code, we store scale2 as a view, with the last entry modified
  ExecViewManaged<Scalar[NUM_LEV_P]> m_scale2;

  const int                   m_rsplit;
  const bool                  m_theta_hydrostatic_mode;
  const AdvectionForm         m_theta_advection_form;

  const HybridVCoord          m_hvcoord;
  const ElementsState         m_state;
  const ElementsDerivedState  m_derived;
  const ElementsGeometry      m_geometry;
  EquationOfState             m_eos;
  Buffers                     m_buffers;
  const deriv_type            m_deriv;

  SphereOperators             m_sphere_ops;

  struct TagPreExchange {};
  struct TagPostExchange {};

  // Policies
  Kokkos::TeamPolicy<ExecSpace, TagPreExchange>   m_policy_pre;
  Kokkos::RangePolicy<ExecSpace, TagPostExchange> m_policy_post;

  Kokkos::TeamPolicy<ExecSpace, TagPreExchange>
  get_policy_pre_exchange () const {
    return m_policy_pre;
  }

  Kokkos::RangePolicy<ExecSpace, TagPostExchange>
  get_policy_post_exchange () const {
    return m_policy_post;
  }

  Kokkos::Array<std::shared_ptr<BoundaryExchange>, NUM_TIME_LEVELS> m_bes;

  CaarFunctorImpl(const Elements &elements, const Tracers &/* tracers */,
                  const ReferenceElement &ref_FE, const HybridVCoord &hvcoord,
                  const SphereOperators &sphere_ops, const SimulationParams& params)
      : m_rsplit(params.rsplit)
      , m_theta_hydrostatic_mode(params.theta_hydrostatic_mode)
      , m_theta_advection_form(params.theta_adv_form)
      , m_hvcoord(hvcoord)
      , m_state(elements.m_state)
      , m_derived(elements.m_derived)
      , m_geometry(elements.m_geometry)
      , m_deriv(ref_FE.get_deriv())
      , m_sphere_ops(sphere_ops)
      , m_policy_pre (Homme::get_default_team_policy<ExecSpace,TagPreExchange>(elements.num_elems()))
      , m_policy_post (0,elements.num_elems()*NP*NP)
  {
    // Initialize equation of state
    m_eos.init(params.theta_hydrostatic_mode,m_hvcoord);

    // Make sure the buffers in sph op are large enough for this functor's needs
    m_sphere_ops.allocate_buffers(m_policy_pre);

    m_scale2 = decltype(m_scale2)("");
  }

  int requested_buffer_size () const {
    // Ask the buffers manager to allocate enough buffers to satisfy Caar's needs
    const int nteams = get_num_concurrent_teams(m_policy_pre);

    // Do 2d scalar and 3d interface scalar right away
    int num_scalar_mid = Buffers::num_3d_scalar_mid_buf;
    if (m_theta_hydrostatic_mode) {
      --num_scalar_mid;
    }
    return num_scalar_mid*NP*NP*NUM_LEV*VECTOR_SIZE*nteams
         + Buffers::num_3d_scalar_int_buf*NP*NP*NUM_LEV_P*VECTOR_SIZE*nteams
         + Buffers::num_3d_vector_mid_buf*2*NP*NP*NUM_LEV*VECTOR_SIZE*nteams
         + Buffers::num_3d_vector_int_buf*2*NP*NP*NUM_LEV_P*VECTOR_SIZE*nteams;
  }

  void init_buffers (const FunctorsBuffersManager& fbm) {
    Errors::runtime_check(fbm.allocated_size()>=requested_buffer_size(), "Error! Buffers size not sufficient.\n");

    Scalar* mem = reinterpret_cast<Scalar*>(fbm.get_memory());
    const int nteams = get_num_concurrent_teams(m_policy_pre);

    // Midpoints scalars
    m_buffers.pnh        = decltype(m_buffers.pnh       )(mem,nteams);
    mem += m_buffers.pnh.size();
    if (m_theta_hydrostatic_mode) {
      // pi = pnh
      m_buffers.pi = m_buffers.pnh;
    } else {
      m_buffers.pi       = decltype(m_buffers.pi        )(mem,nteams);
      mem += m_buffers.pi.size();
    }

    m_buffers.temp       = decltype(m_buffers.temp      )(mem,nteams);
    mem += m_buffers.temp.size();
    m_buffers.exner      = decltype(m_buffers.exner     )(mem,nteams);
    mem += m_buffers.exner.size();
    m_buffers.phi        = decltype(m_buffers.phi       )(mem,nteams);
    mem += m_buffers.phi.size();
    m_buffers.div_vdp    = decltype(m_buffers.div_vdp   )(mem,nteams);
    mem += m_buffers.div_vdp.size();
    m_buffers.omega_p    = decltype(m_buffers.omega_p   )(mem,nteams);
    mem += m_buffers.omega_p.size();
    m_buffers.vort       = decltype(m_buffers.vort)(mem,nteams);
    mem += m_buffers.vort.size();
    m_buffers.theta_vadv = decltype(m_buffers.theta_vadv)(mem,nteams);
    mem += m_buffers.theta_vadv.size();
    m_buffers.theta_tens = decltype(m_buffers.theta_tens)(mem,nteams);
    mem += m_buffers.theta_tens.size();
    m_buffers.dp_tens    = decltype(m_buffers.dp_tens   )(mem,nteams);
    mem += m_buffers.dp_tens.size();

    // Midpoints vectors
    m_buffers.v_vadv   = decltype(m_buffers.v_vadv  )(mem,nteams);
    mem += m_buffers.v_vadv.size();
    m_buffers.grad_tmp = decltype(m_buffers.grad_tmp)(mem,nteams);
    mem += m_buffers.grad_tmp.size();
    m_buffers.vdp      = decltype(m_buffers.vdp     )(mem,nteams);
    mem += m_buffers.vdp.size();
    m_buffers.v_tens   = decltype(m_buffers.v_tens  )(mem,nteams);
    mem += m_buffers.v_tens.size();

    // Interface scalars
    m_buffers.dp_i         = decltype(m_buffers.dp_i        )(mem,nteams);
    mem += m_buffers.dp_i.size();
    m_buffers.vtheta_i     = decltype(m_buffers.vtheta_i    )(mem,nteams);
    mem += m_buffers.vtheta_i.size();
    m_buffers.dpnh_dp_i    = decltype(m_buffers.dpnh_dp_i   )(mem,nteams);
    mem += m_buffers.dpnh_dp_i.size();
    m_buffers.w_vadv       = decltype(m_buffers.w_vadv      )(mem,nteams);
    mem += m_buffers.w_vadv.size();
    m_buffers.phi_vadv     = decltype(m_buffers.phi_vadv    )(mem,nteams);
    mem += m_buffers.phi_vadv.size();
    m_buffers.eta_dot_dpdn = decltype(m_buffers.eta_dot_dpdn)(mem,nteams);
    mem += m_buffers.eta_dot_dpdn.size();
    m_buffers.phi_tens     = decltype(m_buffers.phi_tens    )(mem,nteams);
    mem += m_buffers.phi_tens.size();
    m_buffers.w_tens       = decltype(m_buffers.w_tens      )(mem,nteams);
    mem += m_buffers.w_tens.size();

    // Interface vectors
    m_buffers.v_i          = decltype(m_buffers.v_i         )(mem,nteams);
    mem += m_buffers.v_i.size();
    m_buffers.grad_phinh_i = decltype(m_buffers.grad_phinh_i)(mem,nteams);
    mem += m_buffers.grad_phinh_i.size();
    m_buffers.grad_w_i     = decltype(m_buffers.grad_w_i    )(mem,nteams);
  }

  void init_boundary_exchanges (const std::shared_ptr<MpiBuffersManager>& bm_exchange) {
    for (int tl=0; tl<NUM_TIME_LEVELS; ++tl) {
      m_bes[tl] = std::make_shared<BoundaryExchange>();
      auto& be = *m_bes[tl];
      be.set_buffers_manager(bm_exchange);
      if (m_theta_hydrostatic_mode) {
        be.set_num_fields(0,0,4);
      } else {
        be.set_num_fields(0,0,4,2);
      }
      be.register_field(m_state.m_v,tl,2,0);
      be.register_field(m_state.m_vtheta_dp,1,tl);
      be.register_field(m_state.m_dp3d,1,tl);
      if (!m_theta_hydrostatic_mode) {
        // Note: phinh_i at the surface (last level) is constant, so it doesn't *need* bex.
        //       If bex(constant)=constant, we might just do it. This would not eliminate
        //       the need for halo-exchange of interface-based quantities though, since
        //       we would still need to exchange w_i.
        be.register_field(m_state.m_w_i,1,tl);
        be.register_field(m_state.m_phinh_i,1,tl);
      }
      be.registration_completed();
    }
  }

  void set_rk_stage_data (const RKStageData& data) {
    m_data = data;

    // Set m_scale2 to m_data.scale2 everywhere, except on last interface,
    // where we set it to m_data.scale1. While at it, we already multiply by g.
    constexpr auto g = PhysicalConstants::g;

    auto scale2 = Homme::viewAsReal(m_scale2);
    Kokkos::deep_copy(scale2,m_data.scale2*g);
    Kokkos::deep_copy(Kokkos::subview(scale2,NUM_PHYSICAL_LEV),m_data.scale1*g);
  }

  void run (const RKStageData& data)
  {
    set_rk_stage_data(data);

    profiling_resume();
    GPTLstart("caar compute");
    Kokkos::parallel_for("caar loop pre-boundary exchange", m_policy_pre, *this);
    ExecSpace::fence();
    GPTLstop("caar compute");

    start_timer("caar_bexchV");
    m_bes[data.np1]->exchange(m_geometry.m_rspheremp);
    stop_timer("caar_bexchV");

    if (!m_theta_hydrostatic_mode) {
      GPTLstart("caar compute");
      Kokkos::parallel_for("caar loop post-boundary exchange", m_policy_post, *this);
      ExecSpace::fence();
      GPTLstop("caar compute");
    }
    profiling_pause();
  }

  KOKKOS_INLINE_FUNCTION
  void operator()(const TagPreExchange&, const TeamMember &team) const {
    KernelVariables kv(team);

    compute_div_vdp(kv);

    compute_pi_and_omega(kv);

    Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                         [&](const int idx) {
      const int igp = idx / NP; 
      const int jgp = idx % NP;

      // Use EOS to compute pnh, exner, and dpnh_dp_i. Use some unused buffers for pnh_i
      if (m_theta_hydrostatic_mode) {
        // Recall that, in this case, pnh aliases pi, and pi was already computed
        m_eos.compute_exner(kv,Homme::subview(m_buffers.pnh,kv.team_idx,igp,jgp),
                               Homme::subview(m_buffers.exner,kv.team_idx,igp,jgp));
      } else {
        m_eos.compute_pnh_and_exner(kv,
                                    Homme::subview(m_state.m_vtheta_dp,kv.ie,m_data.n0,igp,jgp),
                                    Homme::subview(m_state.m_phinh_i,kv.ie,m_data.n0,igp,jgp),
                                    Homme::subview(m_buffers.pnh,kv.team_idx,igp,jgp),
                                    Homme::subview(m_buffers.exner,kv.team_idx,igp,jgp));
      }
    });

    compute_phi (kv);

    if (m_theta_hydrostatic_mode) {
      // Zero out w
      Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                           [&](const int idx) {
        const int igp = idx / NP;
        const int jgp = idx % NP;
        Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV_P),
                           [&](const int ilev) {
          m_state.m_w_i(kv.ie,m_data.n0,igp,jgp,ilev) = 0.0;
        });
      });
    }

    compute_interface_quantities(kv);

    if (m_rsplit>0) {
      Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                           [&](const int idx) {
        const int igp = idx / NP; 
        const int jgp = idx % NP;

        Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                             [&](const int ilev) {
          m_buffers.v_vadv(kv.team_idx,0,igp,jgp,ilev) = 0;
          m_buffers.v_vadv(kv.team_idx,1,igp,jgp,ilev) = 0;
          m_buffers.w_vadv(kv.team_idx,igp,jgp,ilev) = 0;
          m_buffers.eta_dot_dpdn(kv.team_idx,igp,jgp,ilev) = 0;
          m_buffers.theta_vadv(kv.team_idx,igp,jgp,ilev) = 0;
          m_buffers.phi_vadv(kv.team_idx,igp,jgp,ilev) = 0;
        });
        // Zero out last interface pack.
        if (NUM_LEV!=NUM_LEV_P) {
          constexpr int lastPack = ColInfo<NUM_INTERFACE_LEV>::LastPack;
          Kokkos::single(Kokkos::PerThread(kv.team),[&](){
            m_buffers.w_vadv(kv.team_idx,igp,jgp,lastPack) = 0;
            m_buffers.eta_dot_dpdn(kv.team_idx,igp,jgp,lastPack) = 0;
            m_buffers.phi_vadv(kv.team_idx,igp,jgp,lastPack) = 0;
          });
        }
      });
    } else {
      compute_vertical_advection(kv);
    }

    compute_accumulated_quantities(kv);

    // Compute update quantities
    if (!m_theta_hydrostatic_mode) {
      compute_w_and_phi_tens (kv);
    }
    compute_dp_and_theta_tens (kv);
    compute_v_tens (kv);

    // Update states
    if (!m_theta_hydrostatic_mode) {
      compute_w_and_phi_np1(kv);
    }
    compute_dp3d_and_theta_np1(kv);
    compute_v_np1(kv);
  }

  KOKKOS_INLINE_FUNCTION
  void operator()(const TagPostExchange&, const int idx) const {
    using InfoM = ColInfo<NUM_PHYSICAL_LEV>;
    using InfoI = ColInfo<NUM_INTERFACE_LEV>;

    // Note: make sure you run this only in non-hydro mode
    // KernelVariables kv(team);
    const int ie  = idx / (NP*NP);
    const int igp = (idx / NP) % NP;
    const int jgp =  idx % NP;

    auto& u = m_state.m_v(ie,m_data.np1,0,igp,jgp,InfoM::LastPack)[InfoM::LastVecEnd];
    auto& v = m_state.m_v(ie,m_data.np1,1,igp,jgp,InfoM::LastPack)[InfoM::LastVecEnd];
    auto& w = m_state.m_w_i(ie,m_data.np1,igp,jgp,InfoI::LastPack)[InfoI::LastVecEnd];
    constexpr auto g = PhysicalConstants::g;
    const auto& phis_x = m_geometry.m_gradphis(ie,0,igp,jgp);
    const auto& phis_y = m_geometry.m_gradphis(ie,1,igp,jgp);

    // Compute dpnh_dp_i on surface
    auto dpnh_dp_i = 1 + ( ( (u*phis_x + v*phis_y)/g - w) /
                             (g + (phis_x*phis_x+phis_y*phis_y)/(2*g) ) ) / m_data.dt;

    // Update w_i on bottom interface
    // Update v on bottom level
    w += m_data.scale1*m_data.dt*g*(dpnh_dp_i-1.0);
    u -= m_data.scale1*m_data.dt*(dpnh_dp_i-1.0)*phis_x/2.0;
    v -= m_data.scale1*m_data.dt*(dpnh_dp_i-1.0)*phis_y/2.0;

    // TODO: you need to modify the BoundaryExchange class a bit, cause as of today
    //       it exchanges *all* vertical levels. For phi, we don't want/need to
    //       exchange the last level, since phi=phis at surface.
    //       So to make sure we're not messing up, set phi back to phis on last interface
    // Note: this is *independent* of whether NUM_LEV==NUM_LEV_P or not.
    auto& phi = m_state.m_phinh_i(ie,m_data.np1,igp,jgp,InfoI::LastPack)[InfoI::LastVecEnd];
    phi = m_geometry.m_phis(ie,igp,jgp);
  }

  KOKKOS_INLINE_FUNCTION
  void compute_div_vdp(KernelVariables &kv) const {
    // Compute vdp
    Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team, NP * NP),
                         [&](const int idx) {
      const int igp = idx / NP; 
      const int jgp = idx % NP; 

      auto v = Homme::subview(m_state.m_v,kv.ie,m_data.n0);
      auto dp3d = Homme::subview(m_state.m_dp3d,kv.ie,m_data.n0,igp,jgp);
      auto vdp = Homme::subview(m_buffers.vdp,kv.team_idx);
      Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team, NUM_LEV), [&] (const int& ilev) {
        vdp(0,igp,jgp,ilev) = v(0,igp,jgp,ilev)*dp3d(ilev);
        vdp(1,igp,jgp,ilev) = v(1,igp,jgp,ilev)*dp3d(ilev);
      }); 
    }); 
    kv.team_barrier();

    // Compute div(vdp)
    m_sphere_ops.divergence_sphere(kv,
        Homme::subview(m_buffers.vdp, kv.team_idx),
        Homme::subview(m_buffers.div_vdp, kv.team_idx));
    kv.team_barrier();
  }

  KOKKOS_INLINE_FUNCTION
  void compute_pi_and_omega(KernelVariables &kv) const {
    Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                         [&](const int idx) {
      const int igp = idx / NP; 
      const int jgp = idx % NP;

      // At interfaces, pi_i(k+1) = pi_i(k)+dp(k), with pi_i(0)=hyai(0)*ps0;
      // then, pi(k) = pi_i(k) + dp(k)/2. Hence, accumulate midpoints to interfaces,
      // then do a parallel_for to add the dp(k)/2 contribution.
      // For omega, we have omega_i(k+1) = omega_i(k)+div_vdp(k), with omega_i(0)=0;
      // then, omega(k) = v*grad(pi) - average(omega_i). You can see how we could
      // avoid computing omega_i, and set the accumulation step to be
      // omega(k+1) = omega(k)+(div_vdp(k)+div_vdp(k-1))/2, with omega(0)=div_vdp(0)/2.
      // However, avoiding computing omega_i would lose BFB agreement
      // with the original F90 implementation. So for now, keep
      // the two step calculation for omega (interface, then midpoints)
      // TODO; skip calculation of omega_i
      // Note: pi_i and omega_i are not needed after computing pi and omega,
      //       so simply grab unused buffers
      auto dp      = Homme::subview(m_state.m_dp3d,kv.ie,m_data.n0,igp,jgp);
      auto div_vdp = Homme::subview(m_buffers.div_vdp,kv.team_idx,igp,jgp);
      auto pi      = Homme::subview(m_buffers.pi,kv.team_idx,igp,jgp);
      auto omega_i = Homme::subview(m_buffers.dpnh_dp_i,kv.team_idx,igp,jgp);
      auto pi_i = Homme::subview(m_buffers.dpnh_dp_i,kv.team_idx,igp,jgp);

      Kokkos::single(Kokkos::PerThread(kv.team),[&]() {
        pi_i(0)[0] = m_hvcoord.ps0*m_hvcoord.hybrid_ai0;
      });

      ColumnOps::column_scan_mid_to_int<true>(kv,dp,pi_i);

      // Add dp(k)/2 to pi(k)
      Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                           [&](const int ilev) {
        pi(ilev) = pi_i(ilev) + dp(ilev)/2;
      });

      Kokkos::single(Kokkos::PerThread(kv.team),[&]() {
        omega_i(0)[0] = 0.0;
      });

      ColumnOps::column_scan_mid_to_int<true>(kv,div_vdp,omega_i);
      // Average omega_i to midpoints, and change sign, since later
      //   omega=v*grad(pi)-average(omega_i)
      auto omega = Homme::subview(m_buffers.omega_p,kv.team_idx,igp,jgp);
      ColumnOps::compute_midpoint_values<CombineMode::Scale>(kv,omega_i,omega,-1.0);
    });
    kv.team_barrier();

    // Compute grad(pi)
    m_sphere_ops.gradient_sphere(kv,Homme::subview(m_buffers.pi,kv.team_idx),
                                    Homme::subview(m_buffers.grad_tmp,kv.team_idx));
    kv.team_barrier();

    // Update omega with v*grad(p)
    Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                         [&](const int idx) {
      const int igp = idx / NP; 
      const int jgp = idx % NP;

      auto omega = Homme::subview(m_buffers.omega_p,kv.team_idx,igp,jgp);
      auto v = Homme::subview(m_state.m_v,kv.ie,m_data.n0);
      auto grad_tmp = Homme::subview(m_buffers.grad_tmp,kv.team_idx);
      Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                           [&](const int ilev) {
        omega(ilev) += (v(0,igp,jgp,ilev)*grad_tmp(0,igp,jgp,ilev) +
                        v(1,igp,jgp,ilev)*grad_tmp(1,igp,jgp,ilev));
      });
    });
  }

  KOKKOS_INLINE_FUNCTION
  void compute_interface_quantities(KernelVariables &kv) const {
    Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                         [&](const int idx) {
      const int igp = idx / NP; 
      const int jgp = idx % NP; 

      auto u    = Homme::subview(m_state.m_v,kv.ie,m_data.n0,0,igp,jgp);
      auto v    = Homme::subview(m_state.m_v,kv.ie,m_data.n0,1,igp,jgp);
      auto u_i  = Homme::subview(m_buffers.v_i,kv.team_idx,0,igp,jgp);
      auto v_i  = Homme::subview(m_buffers.v_i,kv.team_idx,1,igp,jgp);
      auto dp   = Homme::subview(m_state.m_dp3d,kv.ie,m_data.n0,igp,jgp);
      auto dp_i = Homme::subview(m_buffers.dp_i,kv.team_idx,igp,jgp);

      // Compute interface dp
      ColumnOps::compute_interface_values(kv.team,dp,dp_i);

      // Compute interface horiz velocity
      ColumnOps::compute_interface_values(kv.team,dp,dp_i,u,u_i);
      ColumnOps::compute_interface_values(kv.team,dp,dp_i,v,v_i);

      // Compute interface vtheta_i, with an energy preserving scheme

      // w_vadv is yet to be computed, so the buffer is available
      auto vtheta_i = Homme::subview(m_buffers.vtheta_i,kv.team_idx,igp,jgp);
      auto dexner_i = Homme::subview(m_buffers.w_vadv,kv.team_idx,igp,jgp);
      auto exner = Homme::subview(m_buffers.exner,kv.team_idx,igp,jgp);
      auto phi = Homme::subview(m_buffers.phi,kv.team_idx,igp,jgp);
      auto dpnh_dp_i = Homme::subview(m_buffers.dpnh_dp_i,kv.team_idx,igp,jgp);

      m_eos.compute_dpnh_dp_i(kv,Homme::subview(m_buffers.pnh,kv.team_idx,igp,jgp),
                                 dp_i,
                                 dpnh_dp_i);

      // vtheta_i(k) = -dpnh_dp_i(k)*(phi(k)-phi(k-1)) / (exner(k)-exner(k-1)) / Cp
      ColumnOps::compute_interface_delta(kv,phi,vtheta_i);
      // Set dexner_i to 1 at top/bottom, to avoid 0/0
      // Note: to specify bctype, you must specify combinemode too.
      ColumnOps::compute_interface_delta<CombineMode::Replace,BCType::Value>(kv,exner,dexner_i,1.0);
      Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                           [&](const int ilev) {
        vtheta_i(ilev) *= -dpnh_dp_i(ilev);
        vtheta_i(ilev) /= dexner_i(ilev);
        vtheta_i(ilev) /= PhysicalConstants::cp;
      });
    });
    kv.team_barrier();
  }

  KOKKOS_INLINE_FUNCTION
  void compute_phi(KernelVariables &kv) const {
    Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                         [&](const int idx) {
      const int igp = idx / NP; 
      const int jgp = idx % NP;

      // Compute phi_i (if hydrostatic mode)
      if (m_theta_hydrostatic_mode) {
        m_eos.compute_phi_i(kv, m_geometry.m_phis(kv.ie,igp,jgp),
                            Homme::subview(m_state.m_vtheta_dp,kv.ie,m_data.n0,igp,jgp),
                            Homme::subview(m_buffers.exner,kv.team_idx,igp,jgp),
                            Homme::subview(m_buffers.pnh,kv.team_idx,igp,jgp),
                            Homme::subview(m_state.m_phinh_i,kv.ie,m_data.n0,igp,jgp));
      }

      // Compute phi at midpoints
      ColumnOps::compute_midpoint_values(kv,Homme::subview(m_state.m_phinh_i,kv.ie,m_data.n0,igp,jgp),
                                            Homme::subview(m_buffers.phi,kv.team_idx,igp,jgp));
    });
    kv.team_barrier();
  }

  KOKKOS_INLINE_FUNCTION
  void compute_vertical_advection(KernelVariables &kv) const {
    // Compute vertical advection terms:
    //  - eta_dot_dpdn
    //  - v_vadv
    //  - theta_vadv
    //  - phi_vadv_i
    //  - w_vadv_i
    Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                         [&](const int idx) {
      const int igp = idx / NP; 
      const int jgp = idx % NP;

      compute_eta_dot_dpn (kv,igp,jgp);
      compute_v_vadv      (kv,igp,jgp);
      compute_vtheta_vadv (kv,igp,jgp);
      compute_w_vadv      (kv,igp,jgp);
      compute_phi_vadv    (kv,igp,jgp);
    });

    kv.team_barrier();
  }

  KOKKOS_INLINE_FUNCTION
  void compute_eta_dot_dpn (KernelVariables& kv, const int& igp, const int& jgp) const {
    auto div_vdp = viewAsReal(Homme::subview(m_buffers.div_vdp,kv.team_idx,igp,jgp));
    auto eta_dot_dpdn = viewAsReal(Homme::subview(m_buffers.eta_dot_dpdn,kv.team_idx,igp,jgp));

    // Integrate -vdp
    Kokkos::parallel_scan(Kokkos::ThreadVectorRange(kv.team,NUM_PHYSICAL_LEV),
                          [&](const int ilev, Real& accumulator, const bool last) {
      accumulator += div_vdp(ilev);
      if (last) {
        eta_dot_dpdn(ilev+1) = -accumulator;
      }
    });

    // Get the last entry, which is the sum over the whole column
    const Real eta_dot_dpdn_last = -eta_dot_dpdn(NUM_INTERFACE_LEV-1);

    Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,1,NUM_PHYSICAL_LEV),
                         [&](const int ilev) {
      eta_dot_dpdn(ilev) += m_hvcoord.hybrid_bi(ilev)*eta_dot_dpdn_last;
    });

    // Set the boundary conditions for eta_dot_dpdn
    Kokkos::single(Kokkos::PerThread(kv.team),[&](){
      eta_dot_dpdn(0) = eta_dot_dpdn(NUM_INTERFACE_LEV-1) = 0;
    });
  }

  KOKKOS_INLINE_FUNCTION
  void compute_v_vadv (KernelVariables& kv, const int& igp, const int& jgp) const {
    auto u  = viewAsReal(Homme::subview(m_state.m_v,kv.ie,m_data.n0,0,igp,jgp));
    auto v  = viewAsReal(Homme::subview(m_state.m_v,kv.ie,m_data.n0,1,igp,jgp));
    auto dp = viewAsReal(Homme::subview(m_state.m_dp3d,kv.ie,m_data.n0,igp,jgp));
    auto eta_dot_dpdn = viewAsReal(Homme::subview(m_buffers.eta_dot_dpdn,kv.team_idx,igp,jgp));
    auto u_vadv = viewAsReal(Homme::subview(m_buffers.v_vadv,kv.team_idx,0,igp,jgp));
    auto v_vadv = viewAsReal(Homme::subview(m_buffers.v_vadv,kv.team_idx,1,igp,jgp));

    Real facp, facm;

    // TODO: vectorize this code.
    facp = 0.5*(eta_dot_dpdn(1)/dp(0));
    u_vadv(0) = facp*(u(1)-u(0));
    v_vadv(0) = facp*(v(1)-v(0));

    Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,1,NUM_PHYSICAL_LEV-1),
                         [&](const int k) {
      facp = 0.5*eta_dot_dpdn(k+1)/dp(k);
      facm = 0.5*eta_dot_dpdn(k)/dp(k);
      u_vadv(k) = facp*(u(k+1)-u(k)) + facm*(u(k)-u(k-1));
      v_vadv(k) = facp*(v(k+1)-v(k)) + facm*(v(k)-v(k-1));
    });
    
    constexpr int last = NUM_PHYSICAL_LEV-1;
    facm = 0.5*(eta_dot_dpdn(last)/dp(last));
    u_vadv(last) = facm*(u(last)-u(last-1));
    v_vadv(last) = facm*(v(last)-v(last-1));
  }

  KOKKOS_INLINE_FUNCTION
  void compute_vtheta_vadv (KernelVariables& kv, const int& igp, const int& jgp) const {
    auto eta_dot_dpdn = Homme::subview(m_buffers.eta_dot_dpdn,kv.team_idx,igp,jgp);
    auto vtheta_i = Homme::subview(m_buffers.vtheta_i,kv.team_idx,igp,jgp);
    // No point in going till NUM_LEV_P, since vtheta_i=0 at the bottom
    // Also, this is the last time we need vtheta_i, so we can overwrite it.

    auto provider = [&](const int ilev)->Scalar{
      return eta_dot_dpdn(ilev)*vtheta_i(ilev);
    };

    auto theta_vadv = Homme::subview(m_buffers.theta_vadv,kv.team_idx,igp,jgp);
    ColumnOps::compute_midpoint_delta(kv,provider,theta_vadv);
  }

  KOKKOS_INLINE_FUNCTION
  void compute_w_vadv (KernelVariables& kv, const int& igp, const int& jgp) const {
    // w_vadv = average(temp)/dp3d_i.
    // temp = average(eta_dot)*delta(w_i)
    auto temp = Homme::subview(m_buffers.temp,kv.team_idx,igp,jgp);
    auto w_i = Homme::subview(m_state.m_w_i,kv.ie,m_data.n0,igp,jgp);
    auto dp_i = Homme::subview(m_buffers.dp_i,kv.team_idx,igp,jgp);
    auto eta_dot_dpdn = Homme::subview(m_buffers.eta_dot_dpdn,kv.team_idx,igp,jgp);
    auto w_vadv = Homme::subview(m_buffers.w_vadv,kv.team_idx,igp,jgp);

    ColumnOps::compute_midpoint_delta(kv,w_i,temp);
    ColumnOps::compute_midpoint_values<CombineMode::ProdUpdate>(kv,eta_dot_dpdn,temp);
    
    ColumnOps::compute_interface_values(kv,temp,w_vadv);
    Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV_P),
                         [&](const int ilev) {
      w_vadv(ilev) /= dp_i(ilev);
    });
  }

  KOKKOS_INLINE_FUNCTION
  void compute_phi_vadv (KernelVariables& kv, const int& igp, const int& jgp) const {
    // phi_vadv = eta_dot*delta(phi)/dp3d_i.
    auto phi = Homme::subview(m_buffers.phi,kv.team_idx,igp,jgp);
    auto phi_vadv = Homme::subview(m_buffers.phi_vadv,kv.team_idx,igp,jgp);
    auto eta_dot_dpdn = Homme::subview(m_buffers.eta_dot_dpdn,kv.team_idx,igp,jgp);
    auto dp_i = Homme::subview(m_buffers.dp_i,kv.team_idx,igp,jgp);

    ColumnOps::compute_interface_delta(kv,phi,phi_vadv);
    Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                         [&](const int ilev) {
      phi_vadv(ilev) *= eta_dot_dpdn(ilev);
      phi_vadv(ilev) /= dp_i(ilev);
    });
  }

  KOKKOS_INLINE_FUNCTION
  void compute_accumulated_quantities(KernelVariables &kv) const {
    // Compute omega = v*grad(p) + average(omega_i)
    // Accmuulate: dereived.omega_p += eta_ave_w*omega
    // Accmuulate: dereived.eta_dot_dpdn += eta_ave_w*eta_dot_dpdn
    // Accumulate: vn0 += eta_ave_w*v*dp
    Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                         [&](const int idx) {
      const int igp = idx / NP; 
      const int jgp = idx % NP;

      // Accumulate eta_dot_dpdn
      // Note: m_buffers.eta_dot_dpdn is 0 at top/bottom, so we can just accumulate NUM_LEV packs
      Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                           [&](const int ilev) {
        m_derived.m_eta_dot_dpdn(kv.ie,igp,jgp,ilev) +=
              m_data.eta_ave_w*m_buffers.eta_dot_dpdn(kv.team_idx,igp,jgp,ilev);
      });

      // Compute omega = v*grad(p)+average(omega_i) and accumulate
      // Note: so far omega already contains average(omega_i), since we didn't even
      //       bother storing omega_i, and computed directly the average
      Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                           [&](const int ilev) {
        m_derived.m_omega_p(kv.ie,igp,jgp,ilev) +=
              m_data.eta_ave_w*m_buffers.omega_p(kv.team_idx,igp,jgp,ilev);
      });

      // Accumulate v*dp
      Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                           [&](const int ilev) {
        m_derived.m_vn0(kv.ie,0,igp,jgp,ilev) += m_data.eta_ave_w*m_buffers.vdp(kv.team_idx,0,igp,jgp,ilev);
        m_derived.m_vn0(kv.ie,1,igp,jgp,ilev) += m_data.eta_ave_w*m_buffers.vdp(kv.team_idx,1,igp,jgp,ilev);
      });
    });
    kv.team_barrier();
  }

  KOKKOS_INLINE_FUNCTION
  void compute_w_and_phi_tens (KernelVariables& kv) const {
    // Compute grad(phinh_i)
    // Compute v*grad(w_i)
    // Compute w_tens = scale1*(-w_vadv_i - v*grad(w_i)) - scale2*g*(1-dpnh_dp_i)
    // Compute phi_tens = scale1*(-phi_vadv_i - v*grad(phinh_i)) + scale2*g*w_i
    auto grad_w_i = Homme::subview(m_buffers.grad_w_i,kv.team_idx);
    auto grad_phinh_i = Homme::subview(m_buffers.grad_phinh_i,kv.team_idx);
    m_sphere_ops.gradient_sphere(kv,Homme::subview(m_state.m_phinh_i,kv.ie,m_data.n0),
                                    grad_phinh_i);
    m_sphere_ops.gradient_sphere(kv,Homme::subview(m_state.m_w_i,kv.ie,m_data.n0),
                                    grad_w_i);

    auto v_i = Homme::subview(m_buffers.v_i,kv.team_idx);
    Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                         [&](const int idx) {
      const int igp = idx / NP;
      const int jgp = idx % NP;

      auto w_tens = Homme::subview(m_buffers.w_tens,kv.team_idx,igp,jgp);
      auto phi_tens = Homme::subview(m_buffers.phi_tens,kv.team_idx,igp,jgp);

      Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV_P),
                           [&](const int ilev) {

#ifdef XX_NONBFB_COMING
        // TODO: defer multiplication by scale1 until calculation of np1 quantities,
        //       so you can save one packed op*, by multiplying by scale1*dt
#endif

        // Compute w_tens
        auto temp = m_buffers.dpnh_dp_i(kv.team_idx,igp,jgp,ilev);
        temp -= 1.0;
        temp *= m_scale2(ilev);

        w_tens(ilev)  = v_i(0,igp,jgp,ilev)*grad_w_i(0,igp,jgp,ilev);
        w_tens(ilev) += v_i(1,igp,jgp,ilev)*grad_w_i(1,igp,jgp,ilev);
        w_tens(ilev) += m_buffers.w_vadv(kv.team_idx,igp,jgp,ilev);
        w_tens(ilev) *= -m_data.scale1;
        w_tens(ilev) += temp;

        // Compute phi_tens
        temp = m_state.m_w_i(kv.ie,m_data.n0,igp,jgp,ilev);
        temp *= m_scale2(ilev);

        phi_tens(ilev)  = v_i(0,igp,jgp,ilev)*grad_phinh_i(0,igp,jgp,ilev);
        phi_tens(ilev) += v_i(1,igp,jgp,ilev)*grad_phinh_i(1,igp,jgp,ilev);
        phi_tens(ilev) += m_buffers.phi_vadv(kv.team_idx,igp,jgp,ilev);
        phi_tens(ilev) *= -m_data.scale1;
        phi_tens(ilev) += temp;
      });
    });
    kv.team_barrier();
  }

  KOKKOS_INLINE_FUNCTION
  void compute_w_and_phi_np1(KernelVariables &kv) const {
    // Update w_i(np1) = spheremp*(scale3*w_i(nm1) + dt*w_tens)
    // Update phi_i(np1) = spheremp*(scale3*phi_i(nm1) + dt*phi_tens)

    Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                         [&](const int idx) {
      const int igp = idx / NP;
      const int jgp = idx % NP;

      auto spheremp = m_geometry.m_spheremp(kv.ie,igp,jgp);

      auto w_tens = Homme::subview(m_buffers.w_tens,kv.team_idx,igp,jgp);
      auto phi_tens = Homme::subview(m_buffers.phi_tens,kv.team_idx,igp,jgp);

      auto w_np1 = Homme::subview(m_state.m_w_i,kv.ie,m_data.np1,igp,jgp);
      auto phi_np1 = Homme::subview(m_state.m_phinh_i,kv.ie,m_data.np1,igp,jgp);

      Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                           [&](const int ilev) {

        // Add w_tens to w_nm1 and multiply by spheremp
#ifdef XX_NONBFB_COMING
        w_tens(ilev) *= m_data.dt*spheremp;
        w_np1(ilev) = m_state.m_w_i(kv.ie,m_data.nm1,igp,jgp,ilev);
        w_np1(ilev) *= m_data.scale3*spheremp;
        w_np1(ilev) += w_tens(ilev);
#else
        w_tens(ilev) *= m_data.dt;
        w_np1(ilev) = m_state.m_w_i(kv.ie,m_data.nm1,igp,jgp,ilev);
        w_np1(ilev) *= m_data.scale3;
        w_np1(ilev) += w_tens(ilev);
        w_np1(ilev) *= spheremp;
#endif

        // Add phi_tens to phi_nm1 and multiply by spheremp
        // temp *= m_data.dt*spheremp;
#ifdef XX_NONBFB_COMING
        phi_tens(ilev) *= m_data.dt*spheremp;
        phi_np1(ilev) = m_state.m_phinh_i(kv.ie,m_data.nm1,igp,jgp,ilev);
        phi_np1(ilev) *= m_data.scale3*spheremp;
        phi_np1(ilev) += phi_tens(ilev);
#else
        phi_tens(ilev) *= m_data.dt;
        phi_np1(ilev) = m_state.m_phinh_i(kv.ie,m_data.nm1,igp,jgp,ilev);
        phi_np1(ilev) *= m_data.scale3;
        phi_np1(ilev) += phi_tens(ilev);
        phi_np1(ilev) *= spheremp;
#endif
      });

      // Last interface only for w, not phi (since phi=phis there)
      Kokkos::single(Kokkos::PerThread(kv.team),[&](){
        using Info = ColInfo<NUM_INTERFACE_LEV>;
        constexpr int ilev = Info::LastPack;
        constexpr int ivec = Info::LastVecEnd;

        // Note: we only have 1 physical entry in the pack,
        // so we may as well operate on the Real level
        if (NUM_LEV==NUM_LEV_P) {
          // We processed last interface too, so fix phi.
          phi_np1(ilev)[ivec] = m_geometry.m_phis(kv.ie,igp,jgp);
        } else {
          // We didn't do anything on last interface, so update w
#ifdef XX_NONBFB_COMING
          w_tens(ilev)[ivec] *= m_data.dt*spheremp;
          w_np1(ilev)[ivec]  = m_state.m_w_i(kv.ie,m_data.nm1,igp,jgp,ilev)[ivec];
          w_np1(ilev)[ivec] *= m_data.scale3*spheremp;
          w_np1(ilev)[ivec] += w_tens(ilev)[ivec];
#else
          w_tens(ilev)[ivec] *= m_data.dt;
          w_np1(ilev)[ivec]  = m_state.m_w_i(kv.ie,m_data.nm1,igp,jgp,ilev)[ivec];
          w_np1(ilev)[ivec] *= m_data.scale3;
          w_np1(ilev)[ivec] += w_tens(ilev)[ivec];
          w_np1(ilev)[ivec] *= spheremp;
#endif
        }
      });
    });
    kv.team_barrier();
  }

  KOKKOS_INLINE_FUNCTION
  void compute_dp_and_theta_tens (KernelVariables &kv) const {
    // Compute dp_tens=scale1*(div(vdp) + delta(eta_dot_dpdn))
    // Compute theta_tens=scale1*(-theta_vadv-div(v*vtheta_dp)
    // Note: div(v*vtheta_dp) can be computed in conservative or
    //       non concervative form (div(ab) vs a*div(b)+grad(a)*b)

    if (m_theta_advection_form==AdvectionForm::Conservative) {
      m_sphere_ops.divergence_product_sphere(kv,Homme::subview(m_state.m_v,kv.ie,m_data.n0),
                                                Homme::subview(m_state.m_vtheta_dp,kv.ie,m_data.n0),
                                                Homme::subview(m_buffers.theta_tens,kv.team_idx));
    } else {
      Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                           [&](const int idx) {
        const int igp = idx / NP;
        const int jgp = idx % NP;
        auto vtheta_dp = Homme::subview(m_state.m_vtheta_dp,kv.ie,m_data.n0,igp,jgp);
        auto dp        = Homme::subview(m_state.m_dp3d,kv.ie,m_data.n0,igp,jgp);
        auto vtheta    = Homme::subview(m_buffers.temp,kv.team_idx,igp,jgp);

        Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                             [&](const int ilev) {
          vtheta(ilev) = vtheta_dp(ilev) / dp(ilev);
        });
      });
      m_sphere_ops.gradient_sphere(kv,Homme::subview(m_buffers.temp,kv.team_idx),
                                      Homme::subview(m_buffers.grad_tmp,kv.team_idx));
    }

    Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                         [&](const int idx) {
      const int igp = idx / NP;
      const int jgp = idx % NP;

      auto dp_tens = Homme::subview(m_buffers.dp_tens,kv.team_idx,igp,jgp);
      auto theta_tens = Homme::subview(m_buffers.theta_tens,kv.team_idx,igp,jgp);

      auto eta_dot_dpdn = Homme::subview(m_buffers.eta_dot_dpdn,kv.team_idx,igp,jgp);
      auto div_vdp = Homme::subview(m_buffers.div_vdp,kv.team_idx,igp,jgp);

      ColumnOps::compute_midpoint_delta(kv,eta_dot_dpdn,dp_tens);

      Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                           [&](const int ilev) {
        Scalar temp;

        // Compute dp_tens
        dp_tens(ilev) += div_vdp(ilev);

        // Compute theta_tens
        // NOTE: if the condition is false, then theta_tens already contains div(v*theta*dp) already
        if (m_theta_advection_form==AdvectionForm::NonConservative) {
          // m_buffers.temp is already storing vtheta_dp/dp
          theta_tens(ilev)  = div_vdp(ilev)*m_buffers.temp(kv.team_idx,igp,jgp,ilev);
          theta_tens(ilev) += m_buffers.grad_tmp(kv.team_idx,0,igp,jgp,ilev)*m_buffers.vdp(kv.team_idx,0,igp,jgp,ilev);
          theta_tens(ilev) += m_buffers.grad_tmp(kv.team_idx,1,igp,jgp,ilev)*m_buffers.vdp(kv.team_idx,1,igp,jgp,ilev);
        }
        theta_tens(ilev) += m_buffers.theta_vadv(kv.team_idx,igp,jgp,ilev);
        theta_tens(ilev) *= -m_data.scale1;
      });
    });
  }

  KOKKOS_INLINE_FUNCTION
  void compute_dp3d_and_theta_np1(KernelVariables &kv) const {
    // Update dp3d(np1) = spheremp*(scale3*dp3d(nm1) + dt*dp_tens)
    // Update vtheta_dp(np1) = spheremp*(scale3*vtheta_dp(nm1) + dt*theta_tens)

    Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                         [&](const int idx) {
      const int igp = idx / NP;
      const int jgp = idx % NP;

      const auto& spheremp = m_geometry.m_spheremp(kv.ie,igp,jgp);

      auto dp_tens = Homme::subview(m_buffers.dp_tens,kv.team_idx,igp,jgp);
      auto theta_tens = Homme::subview(m_buffers.theta_tens,kv.team_idx,igp,jgp);
      auto dp_np1 = Homme::subview(m_state.m_dp3d,kv.ie,m_data.np1,igp,jgp);
      auto vtheta_np1 = Homme::subview(m_state.m_vtheta_dp,kv.ie,m_data.np1,igp,jgp);

      Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                           [&](const int ilev) {

        // Add dp_tens to dp_nm1 and multiply by spheremp
#ifdef XX_NONBFB_COMING
        dp_tens(ilev) *= m_data.scale1*m_data.dt*spheremp;
        dp_np1(ilev)   = m_data.scale3*spheremp*m_state.m_dp3d(kv.ie,m_data.nm1,igp,jgp,ilev);
        dp_np1(ilev)  -= dp_tens(ilev);
#else
        dp_tens(ilev) *= m_data.scale1*m_data.dt;
        dp_np1(ilev)   = m_data.scale3*m_state.m_dp3d(kv.ie,m_data.nm1,igp,jgp,ilev);
        dp_np1(ilev)  -= dp_tens(ilev);
        dp_np1(ilev)  *= spheremp;
#endif

        // Add theta_tens to vtheta_nm1 and multiply by spheremp
#ifdef XX_NONBFB_COMING
        theta_tens(ilev) *= m_data.dt*spheremp;
        vtheta_np1(ilev)  = m_state.m_vtheta_dp(kv.ie,m_data.nm1,igp,jgp,ilev);
        vtheta_np1(ilev) *= m_data.scale3*spheremp;
        vtheta_np1(ilev) += theta_tens(ilev);
#else
        theta_tens(ilev) *= m_data.dt;
        vtheta_np1(ilev)  = m_state.m_vtheta_dp(kv.ie,m_data.nm1,igp,jgp,ilev);
        vtheta_np1(ilev) *= m_data.scale3;
        vtheta_np1(ilev) += theta_tens(ilev);
        vtheta_np1(ilev) *= spheremp;
#endif
      });
    });
    kv.team_barrier();
  }

  KOKKOS_INLINE_FUNCTION
  void compute_v_tens (KernelVariables &kv) const {
    // Not necessarily in this order,
    //  - Compute vort=vorticity(v)
    //  - Compute wvor = grad[average(w_i^2/2)] - average[w_i*grad(w_i)]
    //  - Compute gradKE = grad(v*v/2)
    //  - Compute gradExner = grad(exner)
    //  - Compute mgrad = average[dpnh_dp_i*gradphinh_i]
    //  - Compute v_tens = 
    //           scale1*(-v_vadv + v2*(fcor+vort)-gradKE -mgrad  -cp*vtheta*gradExner - wvor)
    //           scale1*(-v_vadv - v1*(fcor+vort)-gradKE -mgrad  -cp*vtheta*gradExner - wvor)

    auto v_tens = Homme::subview(m_buffers.v_tens,kv.team_idx);
    auto vort  = Homme::subview(m_buffers.vort,kv.team_idx);

    // Compute vorticity(v)
    m_sphere_ops.vorticity_sphere(kv, Homme::subview(m_state.m_v,kv.ie,m_data.n0),
                                      vort);

    // Compute average(w^2/2)
    Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                         [&](const int idx) {
      const int igp = idx / NP;
      const int jgp = idx % NP;
      auto w_i = Homme::subview(m_state.m_w_i,kv.ie,m_data.n0,igp,jgp);

      // Use lambda as a provider for w_i*w_i
      const auto w_sq = [&w_i] (const int ilev) -> Scalar{
        return w_i(ilev)*w_i(ilev);
      };

      // Rescale by 2 (we are averaging kinetic energy w_i*w_i/2, but the provider has no '/2')
      ColumnOps::compute_midpoint_values<CombineMode::Scale>(kv, w_sq, Homme::subview(m_buffers.temp,kv.team_idx,igp,jgp),0.5);
    });

    // Compute grad(average(w^2/2))
    auto wvor = Homme::subview(m_buffers.vdp,kv.team_idx);
    m_sphere_ops.gradient_sphere(kv, Homme::subview(m_buffers.temp,kv.team_idx),
                                     wvor);

    // Compute grad(w)
    m_sphere_ops.gradient_sphere(kv, Homme::subview(m_state.m_w_i,kv.ie,m_data.n0),
                                     Homme::subview(m_buffers.v_i,kv.team_idx));

    if (m_theta_hydrostatic_mode) {
      // In nh mode, gradphinh has already been computed, but in hydro mode
      // we skip the whole compute_w_and_phi_tens call
      m_sphere_ops.gradient_sphere(kv,Homme::subview(m_state.m_phinh_i,kv.ie,m_data.n0),
                                      Homme::subview(m_buffers.grad_phinh_i,kv.team_idx));
    }

    // Scalar w_vor,mgrad,w_gradw,gradw2;
    Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                         [&](const int idx) {
      const int igp = idx / NP;
      const int jgp = idx % NP;

      // Compute wvor = grad(average(w^2/2)) - average(w*grad(w))
      // Note: vtens is already storing grad(avg(w^2/2))
      auto gradw_x = Homme::subview(m_buffers.v_i,kv.team_idx,0,igp,jgp);
      auto gradw_y = Homme::subview(m_buffers.v_i,kv.team_idx,1,igp,jgp);
      auto wvor_x = Homme::subview(wvor,0,igp,jgp);
      auto wvor_y = Homme::subview(wvor,1,igp,jgp);
      auto w_i = Homme::subview(m_state.m_w_i,kv.ie,m_data.n0,igp,jgp);

      const auto w_gradw_x = [&gradw_x,&w_i] (const int ilev) -> Scalar {
        return gradw_x(ilev)*w_i(ilev);
      };
      const auto w_gradw_y = [&gradw_y,&w_i] (const int ilev) -> Scalar {
        return gradw_y(ilev)*w_i(ilev);
      };

      ColumnOps::compute_midpoint_values<CombineMode::ScaleAdd>(kv,
                        w_gradw_x, wvor_x, -1.0);
      ColumnOps::compute_midpoint_values<CombineMode::ScaleAdd>(kv,
                        w_gradw_y, wvor_y, -1.0);

      // Compute average(dpnh_dp_i*grad(phinh_i)), and add to wvor
      const auto phinh_i_x = Homme::subview(m_buffers.grad_phinh_i,kv.team_idx,0,igp,jgp);
      const auto phinh_i_y = Homme::subview(m_buffers.grad_phinh_i,kv.team_idx,1,igp,jgp);
      const auto dpnh_dp_i = Homme::subview(m_buffers.dpnh_dp_i,kv.team_idx,igp,jgp);
      const auto prod_x = [&phinh_i_x,&dpnh_dp_i](const int ilev)->Scalar {
        return phinh_i_x(ilev)*dpnh_dp_i(ilev);
      };
      const auto prod_y = [&phinh_i_y,&dpnh_dp_i](const int ilev)->Scalar {
        return phinh_i_y(ilev)*dpnh_dp_i(ilev);
      };

      ColumnOps::compute_midpoint_values<CombineMode::Add>(kv,prod_x,wvor_x);
      ColumnOps::compute_midpoint_values<CombineMode::Add>(kv,prod_y,wvor_y);

      // Compute KE. Also, add fcor to vort
      auto u  = Homme::subview(m_state.m_v,kv.ie,m_data.n0,0,igp,jgp);
      auto v  = Homme::subview(m_state.m_v,kv.ie,m_data.n0,1,igp,jgp);
      auto KE = Homme::subview(m_buffers.temp,kv.team_idx,igp,jgp);
      const auto& fcor = m_geometry.m_fcor(kv.ie,igp,jgp);
      Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                           [&](const int ilev) {
        KE(ilev)  = u(ilev)*u(ilev);
        KE(ilev) += v(ilev)*v(ilev);
        KE(ilev) /= 2.0;

        vort(igp,jgp,ilev) += fcor;
      });
    });

    // Compute grad(KE), and sum into v_vadv
    // m_sphere_ops.gradient_sphere(kv, Homme::subview(m_buffers.temp,kv.team_idx),
                                     // Homme::subview(m_buffers.v_vadv,kv.team_idx));
    m_sphere_ops.gradient_sphere_update(kv, Homme::subview(m_buffers.temp,kv.team_idx),
                                            Homme::subview(m_buffers.v_vadv,kv.team_idx));
    // Compute grad(exner)
    auto grad_exner = Homme::subview(m_buffers.grad_tmp,kv.team_idx);
    m_sphere_ops.gradient_sphere(kv, Homme::subview(m_buffers.exner,kv.team_idx),
                                     grad_exner);

    Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                         [&](const int idx) {
      const int igp = idx / NP;
      const int jgp = idx % NP;

      // Assemble vtens (both components)
      auto u_tens = Homme::subview(m_buffers.v_tens,kv.team_idx,0,igp,jgp);
      auto v_tens = Homme::subview(m_buffers.v_tens,kv.team_idx,1,igp,jgp);
      auto vort = Homme::subview(m_buffers.vort,kv.team_idx,igp,jgp);

      Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                           [&](const int ilev) {

        // grad(exner)*vtheta*cp
        Scalar cp_vtheta = PhysicalConstants::cp *
                           (m_state.m_vtheta_dp(kv.ie,m_data.n0,igp,jgp,ilev) /
                            m_state.m_dp3d(kv.ie,m_data.n0,igp,jgp,ilev));

        u_tens(ilev) = cp_vtheta*grad_exner(0,igp,jgp,ilev);
        v_tens(ilev) = cp_vtheta*grad_exner(1,igp,jgp,ilev);

        // v_vadv + gradKE (added up in v_vadv)
        u_tens(ilev) += m_buffers.v_vadv(kv.team_idx,0,igp,jgp,ilev);
        v_tens(ilev) += m_buffers.v_vadv(kv.team_idx,1,igp,jgp,ilev);

        // // avg(dpnh_dp*gradphinh) + wvor (added up in wvor)
        u_tens(ilev) += wvor(0,igp,jgp,ilev);
        v_tens(ilev) += wvor(1,igp,jgp,ilev);

        // Add +/- v_j*(vort+fcor), where v_j=v for u_tens and v_j=u for v_tens
        u_tens(ilev) -= m_state.m_v(kv.ie,m_data.n0,1,igp,jgp,ilev)*vort(ilev);
        v_tens(ilev) += m_state.m_v(kv.ie,m_data.n0,0,igp,jgp,ilev)*vort(ilev);

#ifdef XX_NONBFB_COMING
        // Defer scaling until vtens is added to v, to avoid one packed op*
#else
        // Scale by scale1
        u_tens(ilev) *= -m_data.scale1;
        v_tens(ilev) *= -m_data.scale1;
#endif
      });
    });
  }

  KOKKOS_INLINE_FUNCTION
  void compute_v_np1(KernelVariables &kv) const {
    // Update v(np1) = spheremp*(scale3*v(nm1) + dt*v_tens)
    // NOTE: quite a few buffers no longer needed in this call of operator() are going to be reused here.

    auto v_tens = Homme::subview(m_buffers.v_tens,kv.team_idx);

    Kokkos::parallel_for(Kokkos::TeamThreadRange(kv.team,NP*NP),
                         [&](const int idx) {
      const int igp = idx / NP;
      const int jgp = idx % NP;

      const auto& spheremp = m_geometry.m_spheremp(kv.ie,igp,jgp);

      // Add vtens to v_nm1 and multiply by spheremp
      auto u_np1 = Homme::subview(m_state.m_v,kv.ie,m_data.np1,0,igp,jgp);
      auto v_np1 = Homme::subview(m_state.m_v,kv.ie,m_data.np1,1,igp,jgp);
      auto u_tens = Homme::subview(m_buffers.v_tens,kv.team_idx,0,igp,jgp);
      auto v_tens = Homme::subview(m_buffers.v_tens,kv.team_idx,1,igp,jgp);

      Kokkos::parallel_for(Kokkos::ThreadVectorRange(kv.team,NUM_LEV),
                           [&](const int ilev) {

        // Add v_tens to v_nm1 and multiply by spheremp
        u_np1(ilev) = m_state.m_v(kv.ie,m_data.nm1,0,igp,jgp,ilev);
        v_np1(ilev) = m_state.m_v(kv.ie,m_data.nm1,1,igp,jgp,ilev);

#ifdef XX_NONBFB_COMING
        u_tens(ilev) *= -m_data.dt*m_data.scale1*spheremp;
        v_tens(ilev) *= -m_data.dt*m_data.scale1*spheremp;

        u_np1(ilev) *= m_data.scale3*spheremp;
        v_np1(ilev) *= m_data.scale3*spheremp;

        u_np1(ilev) += u_tens(ilev);
        v_np1(ilev) += v_tens(ilev);
#else
        u_tens(ilev) *= m_data.dt;
        v_tens(ilev) *= m_data.dt;

        u_np1(ilev) *= m_data.scale3;
        v_np1(ilev) *= m_data.scale3;

        u_np1(ilev) += u_tens(ilev);
        v_np1(ilev) += v_tens(ilev);

        u_np1(ilev) *= spheremp;
        v_np1(ilev) *= spheremp;
#endif
      });
    });

    kv.team_barrier();
  }

  KOKKOS_INLINE_FUNCTION
  size_t shmem_size(const int team_size) const {
    return KernelVariables::shmem_size(team_size);
  }
};

} // Namespace Homme

#endif // HOMMEXX_CAAR_FUNCTOR_IMPL_HPP