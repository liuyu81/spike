// See LICENSE for license details.
#ifndef _RISCV_CSRS_H
#define _RISCV_CSRS_H

#include "common.h"
#include "encoding.h"
// For reg_t:
#include "decode.h"
// For std::unordered_map
#include <unordered_map>
// For std::shared_ptr
#include <memory>
// For std::optional
#include <optional>
// For access_type:
#include "memtracer.h"
#include <cassert>
// For std::optional
#include <optional>

class processor_t;
struct state_t;

enum struct elp_t {
  NO_LP_EXPECTED = 0,
  LP_EXPECTED = 1,
};

// Parent, abstract class for all CSRs
class csr_t {
 public:
  csr_t(processor_t* const proc, const reg_t addr);

  // Throw exception if read/write disallowed.
  virtual void verify_permissions(insn_t insn, bool write) const;

  // read() returns the architectural value of this CSR. No permission
  // checking needed or allowed. Side effects not allowed.
  virtual reg_t read() const noexcept = 0;

  // write() updates the architectural value of this CSR. No
  // permission checking needed or allowed.
  // Child classes must implement unlogged_write()
  void write(const reg_t val) noexcept;

  virtual ~csr_t();

 protected:
  // Return value indicates success; false means no write actually occurred
  virtual bool unlogged_write(const reg_t val) noexcept = 0;

  // Record this CSR update (which has already happened) in the commit log
  void log_write() const noexcept;

  // Record a write to an alternate CSR (e.g. minstreth instead of minstret)
  void log_special_write(const reg_t address, const reg_t val) const noexcept;

  // What value was written to this reg? Default implementation simply
  // calls read(), but a few CSRs are special.
  virtual reg_t written_value() const noexcept;

  processor_t* const proc;
  state_t* const state;
 public:
  const reg_t address;
 private:
  const unsigned csr_priv;
  const bool csr_read_only;

  // For access to written_value() and unlogged_write():
  friend class rv32_high_csr_t;
  friend class rv32_low_csr_t;
};

typedef std::shared_ptr<csr_t> csr_t_p;

// Basic CSRs, with XLEN bits fully readable and writable.
class basic_csr_t: public csr_t {
 public:
  basic_csr_t(processor_t* const proc, const reg_t addr, const reg_t init);

  virtual reg_t read() const noexcept override {
    return val;
  }

 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 private:
  reg_t val;
};

class pmpaddr_csr_t: public csr_t {
 public:
  pmpaddr_csr_t(processor_t* const proc, const reg_t addr);
  virtual void verify_permissions(insn_t insn, bool write) const override;
  virtual reg_t read() const noexcept override;

  // Does a 4-byte access at the specified address match this PMP entry?
  bool match4(reg_t addr) const noexcept;

  // Does the specified range match only a proper subset of this page?
  bool subset_match(reg_t addr, reg_t len) const noexcept;

  // Is the specified access allowed given the pmpcfg privileges?
  bool access_ok(access_type type, reg_t mode, bool hlvx) const noexcept;

  // To check lock bit status from outside like mseccfg
  bool is_locked() const noexcept {
    return cfg & PMP_L;
  }

 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 private:
  // Assuming this is configured as TOR, return address for top of
  // range. Also forms bottom-of-range for next-highest pmpaddr
  // register if that one is TOR.
  reg_t tor_paddr() const noexcept;

  // Assuming this is configured as TOR, return address for bottom of
  // range. This is tor_paddr() from the previous pmpaddr register.
  reg_t tor_base_paddr() const noexcept;

  // Assuming this is configured as NAPOT or NA4, return mask for paddr.
  // E.g. for 4KiB region, returns 0xffffffff_fffff000.
  reg_t napot_mask() const noexcept;

  bool next_locked_and_tor() const noexcept;
  reg_t val;
  friend class pmpcfg_csr_t;  // so he can access cfg
  uint8_t cfg;
  const size_t pmpidx;
};

typedef std::shared_ptr<pmpaddr_csr_t> pmpaddr_csr_t_p;

class pmpcfg_csr_t: public csr_t {
 public:
  pmpcfg_csr_t(processor_t* const proc, const reg_t addr);
  virtual void verify_permissions(insn_t insn, bool write) const override;
  virtual reg_t read() const noexcept override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

class mseccfg_csr_t: public basic_csr_t {
 public:
  mseccfg_csr_t(processor_t* const proc, const reg_t addr);
  virtual void verify_permissions(insn_t insn, bool write) const override;
  bool get_mml() const noexcept;
  bool get_mmwp() const noexcept;
  bool get_rlb() const noexcept;
  bool get_useed() const noexcept;
  bool get_sseed() const noexcept;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

typedef std::shared_ptr<mseccfg_csr_t> mseccfg_csr_t_p;

// For CSRs that have a virtualized copy under another name. Each
// instance of virtualized_csr_t will read/write one of two CSRs,
// based on state.v. E.g. sscratch, stval, etc.
//
// Example: sscratch and vsscratch are both instances of basic_csr_t.
// The csrmap will contain a virtualized_csr_t under sscratch's
// address, plus the vsscratch basic_csr_t under its address.

class virtualized_csr_t: public csr_t {
 public:
  virtualized_csr_t(processor_t* const proc, csr_t_p orig, csr_t_p virt);

  virtual reg_t read() const noexcept override;
  // Instead of using state.v, explicitly request original or virtual:
  reg_t readvirt(bool virt) const noexcept;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
  csr_t_p orig_csr;
  csr_t_p virt_csr;
};

typedef std::shared_ptr<virtualized_csr_t> virtualized_csr_t_p;

// For mepc, sepc, and vsepc
class epc_csr_t: public csr_t {
 public:
  epc_csr_t(processor_t* const proc, const reg_t addr);

  virtual reg_t read() const noexcept override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 private:
  reg_t val;
};

// For mtvec, stvec, and vstvec
class tvec_csr_t: public csr_t {
 public:
  tvec_csr_t(processor_t* const proc, const reg_t addr);

  virtual reg_t read() const noexcept override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 private:
  reg_t val;
};

// For mcause, scause, and vscause
class cause_csr_t: public basic_csr_t {
 public:
  cause_csr_t(processor_t* const proc, const reg_t addr);

  virtual reg_t read() const noexcept override;
};

// For *status family of CSRs
class base_status_csr_t: public csr_t {
 public:
  base_status_csr_t(processor_t* const proc, const reg_t addr);

  bool field_exists(const reg_t which) {
    return (sstatus_write_mask & which) != 0;
  }

 protected:
  reg_t adjust_sd(const reg_t val) const noexcept;
  void maybe_flush_tlb(const reg_t newval) noexcept;
  const bool has_page;
  const reg_t sstatus_write_mask;
  const reg_t sstatus_read_mask;
 private:
  reg_t compute_sstatus_write_mask() const noexcept;
};

typedef std::shared_ptr<base_status_csr_t> base_status_csr_t_p;

// For vsstatus, which is its own separate architectural register
// (unlike sstatus)
// vstatus.sdt is read_only 0 when henvcfg.dte = 0
class vsstatus_csr_t final: public base_status_csr_t {
 public:
  vsstatus_csr_t(processor_t* const proc, const reg_t addr);

  virtual reg_t read() const noexcept override;

 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 private:
  reg_t val;
};

typedef std::shared_ptr<vsstatus_csr_t> vsstatus_csr_t_p;

class mstatus_csr_t final: public base_status_csr_t {
 public:
  mstatus_csr_t(processor_t* const proc, const reg_t addr);

  reg_t read() const noexcept override {
    return val;
  }

 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 private:
  reg_t compute_mstatus_initial_value() const noexcept;
  reg_t val;
};

typedef std::shared_ptr<mstatus_csr_t> mstatus_csr_t_p;

class mnstatus_csr_t final: public basic_csr_t {
 public:
  mnstatus_csr_t(processor_t* const proc, const reg_t addr);
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

// For RV32 CSRs that are split into two, e.g. mstatus/mstatush
// CSRW should only modify the lower half
class rv32_low_csr_t: public csr_t {
 public:
  rv32_low_csr_t(processor_t* const proc, const reg_t addr, csr_t_p orig);
  virtual reg_t read() const noexcept override;
  virtual void verify_permissions(insn_t insn, bool write) const override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
  virtual reg_t written_value() const noexcept override;
 private:
  csr_t_p orig;
};

class rv32_high_csr_t: public csr_t {
 public:
  rv32_high_csr_t(processor_t* const proc, const reg_t addr, csr_t_p orig);
  virtual reg_t read() const noexcept override;
  virtual void verify_permissions(insn_t insn, bool write) const override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
  virtual reg_t written_value() const noexcept override;
 private:
  csr_t_p orig;
};

class aia_rv32_high_csr_t: public rv32_high_csr_t {
 public:
  aia_rv32_high_csr_t(processor_t* const proc, const reg_t addr, csr_t_p orig);
  virtual void verify_permissions(insn_t insn, bool write) const override;
};

// sstatus.sdt is read_only 0 when menvcfg.dte = 0
class sstatus_proxy_csr_t final: public base_status_csr_t {
 public:
  sstatus_proxy_csr_t(processor_t* const proc, const reg_t addr, mstatus_csr_t_p mstatus);

  virtual reg_t read() const noexcept override;

 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 private:
  mstatus_csr_t_p mstatus;
};

typedef std::shared_ptr<sstatus_proxy_csr_t> sstatus_proxy_csr_t_p;

class sstatus_csr_t: public virtualized_csr_t {
 public:
  sstatus_csr_t(processor_t* const proc, sstatus_proxy_csr_t_p orig, vsstatus_csr_t_p virt);

  // Set FS, VS, or XS bits to dirty
  void dirty(const reg_t dirties);
  // Return true if the specified bits are not 00 (Off)
  bool enabled(const reg_t which);
 private:
  sstatus_proxy_csr_t_p orig_sstatus;
  vsstatus_csr_t_p virt_sstatus;
};

typedef std::shared_ptr<sstatus_csr_t> sstatus_csr_t_p;

class misa_csr_t final: public basic_csr_t {
 public:
  misa_csr_t(processor_t* const proc, const reg_t addr, const reg_t max_isa);

  bool extension_enabled(unsigned char ext) const noexcept {
    assert(ext >= 'A' && ext <= 'Z');
    return (read() >> (ext - 'A')) & 1;
  }

  bool extension_enabled_const(unsigned char ext) const noexcept;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 private:
  const reg_t max_isa;
  const reg_t write_mask;
  reg_t dependency(const reg_t val, const char feature, const char depends_on) const noexcept;
};

typedef std::shared_ptr<misa_csr_t> misa_csr_t_p;

class mip_or_mie_csr_t: public csr_t {
 public:
  mip_or_mie_csr_t(processor_t* const proc, const reg_t addr);
  virtual reg_t read() const noexcept override;

  virtual void write_with_mask(const reg_t mask, const reg_t val) noexcept;

 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override final;
  reg_t val;
 private:
  virtual reg_t write_mask() const noexcept = 0;
};

// mip is special because some of the bits are driven by hardware pins
class mip_csr_t: public mip_or_mie_csr_t {
 public:
  mip_csr_t(processor_t* const proc, const reg_t addr);
  virtual reg_t read() const noexcept override final;

  void write_with_mask(const reg_t mask, const reg_t val) noexcept override;

  // Does not log. Used by external things (clint) that wiggle bits in mip.
  void backdoor_write_with_mask(const reg_t mask, const reg_t val) noexcept;
 private:
  virtual reg_t write_mask() const noexcept override;
};

typedef std::shared_ptr<mip_csr_t> mip_csr_t_p;

class mie_csr_t: public mip_or_mie_csr_t {
 public:
  mie_csr_t(processor_t* const proc, const reg_t addr);
 private:
  virtual reg_t write_mask() const noexcept override;
};

typedef std::shared_ptr<mie_csr_t> mie_csr_t_p;

// For sip, hip, hvip, vsip, sie, hie, vsie which are all just (masked
// & shifted) views into mip or mie. Each pair will have one of these
// objects describing the view, e.g. one for sip+sie, one for hip+hie,
// etc.
class generic_int_accessor_t {
 public:
  enum mask_mode_t { NONE, MIDELEG, HIDELEG };

  generic_int_accessor_t(state_t* const state,
                         const reg_t read_mask,
                         const reg_t ip_write_mask,
                         const reg_t ie_write_mask,
                         const mask_mode_t mask_mode,
                         const int shiftamt);
  reg_t ip_read() const noexcept;
  void ip_write(const reg_t val) noexcept;
  reg_t ie_read() const noexcept;
  void ie_write(const reg_t val) noexcept;
  reg_t get_ip_write_mask() { return ip_write_mask; }
 private:
  state_t* const state;
  const reg_t read_mask;
  const reg_t ip_write_mask;
  const reg_t ie_write_mask;
  const bool mask_mideleg;
  const bool mask_hideleg;
  const int shiftamt;
  reg_t deleg_mask() const;
};

typedef std::shared_ptr<generic_int_accessor_t> generic_int_accessor_t_p;

// For all CSRs that are simply (masked & shifted) views into mip
class mip_proxy_csr_t: public csr_t {
 public:
  mip_proxy_csr_t(processor_t* const proc, const reg_t addr, generic_int_accessor_t_p accr);
  virtual void verify_permissions(insn_t insn, bool write) const override;
  virtual reg_t read() const noexcept override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
  generic_int_accessor_t_p accr;
};

// For all CSRs that are simply (masked & shifted) views into mie
class mie_proxy_csr_t: public csr_t {
 public:
  mie_proxy_csr_t(processor_t* const proc, const reg_t addr, generic_int_accessor_t_p accr);
  virtual void verify_permissions(insn_t insn, bool write) const override;
  virtual reg_t read() const noexcept override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 private:
  generic_int_accessor_t_p accr;
};

class mideleg_csr_t: public basic_csr_t {
 public:
  mideleg_csr_t(processor_t* const proc, const reg_t addr);
  virtual void verify_permissions(insn_t insn, bool write) const override;
  virtual reg_t read() const noexcept override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

class medeleg_csr_t: public basic_csr_t {
 public:
  medeleg_csr_t(processor_t* const proc, const reg_t addr);
  virtual void verify_permissions(insn_t insn, bool write) const override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 private:
  const reg_t hypervisor_exceptions;
};

class sip_csr_t: public mip_proxy_csr_t {
 public:
  sip_csr_t(processor_t* const proc, const reg_t addr, generic_int_accessor_t_p accr);
  virtual reg_t read() const noexcept override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

class sie_csr_t: public mie_proxy_csr_t {
 public:
  sie_csr_t(processor_t* const proc, const reg_t addr, generic_int_accessor_t_p accr);
  virtual reg_t read() const noexcept override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 private:
  reg_t val;
};

// For CSRs with certain bits hardwired
class masked_csr_t: public basic_csr_t {
 public:
  masked_csr_t(processor_t* const proc, const reg_t addr, const reg_t mask, const reg_t init);
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 private:
  const reg_t mask;
};

class envcfg_csr_t: public masked_csr_t {
 public:
  envcfg_csr_t(processor_t* const proc, const reg_t addr, const reg_t mask, const reg_t init);
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

// henvcfg.pbmte is read_only 0 when menvcfg.pbmte = 0
// henvcfg.stce is read_only 0 when menvcfg.stce = 0
// henvcfg.hade is read_only 0 when menvcfg.hade = 0
// henvcfg.dte is read_only 0 when menvcfg.dte = 0
// henvcfg.sse is read_only 0 when menvcfg.sse = 0
class henvcfg_csr_t final: public envcfg_csr_t {
 public:
  henvcfg_csr_t(processor_t* const proc, const reg_t addr, const reg_t mask, const reg_t init, csr_t_p menvcfg);
  reg_t read() const noexcept override {
    return (menvcfg->read() | ~(MENVCFG_PBMTE | MENVCFG_STCE | MENVCFG_ADUE | MENVCFG_DTE | MENVCFG_SSE)) & masked_csr_t::read();
  }
  virtual void verify_permissions(insn_t insn, bool write) const override;

 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;

 private:
  csr_t_p menvcfg;
};

// For satp and vsatp
// These are three classes in order to handle the [V]TVM bits permission checks
class base_atp_csr_t: public basic_csr_t {
 public:
  base_atp_csr_t(processor_t* const proc, const reg_t addr);
  bool satp_valid(reg_t val) const noexcept;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 private:
  reg_t compute_new_satp(reg_t val) const noexcept;
};

class satp_csr_t: public base_atp_csr_t {
 public:
  satp_csr_t(processor_t* const proc, const reg_t addr);
  virtual void verify_permissions(insn_t insn, bool write) const override;
};

typedef std::shared_ptr<satp_csr_t> satp_csr_t_p;

class virtualized_satp_csr_t: public virtualized_csr_t {
 public:
  virtualized_satp_csr_t(processor_t* const proc, satp_csr_t_p orig, csr_t_p virt);
  virtual void verify_permissions(insn_t insn, bool write) const override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 private:
  satp_csr_t_p orig_satp;
};

// Forward declaration
class smcntrpmf_csr_t;
typedef std::shared_ptr<smcntrpmf_csr_t> smcntrpmf_csr_t_p;

// For minstret and mcycle, which are always 64 bits, but in RV32 are
// split into high and low halves. The first class always holds the
// full 64-bit value.
class wide_counter_csr_t: public csr_t {
 public:
  wide_counter_csr_t(processor_t* const proc, const reg_t addr, smcntrpmf_csr_t_p config_csr);
  // Always returns full 64-bit value
  virtual reg_t read() const noexcept override;
  void bump(const reg_t howmuch) noexcept;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 private:
  bool is_counting_enabled() const noexcept;
  reg_t val;
  bool written;
  smcntrpmf_csr_t_p config_csr;
};

typedef std::shared_ptr<wide_counter_csr_t> wide_counter_csr_t_p;

class time_counter_csr_t: public csr_t {
 public:
  time_counter_csr_t(processor_t* const proc, const reg_t addr);
  virtual reg_t read() const noexcept override;

  void sync(const reg_t val) noexcept;

 protected:
  virtual bool unlogged_write(const reg_t UNUSED val) noexcept override { return false; };
 private:
  reg_t shadow_val;
};

typedef std::shared_ptr<time_counter_csr_t> time_counter_csr_t_p;

// For a CSR that is an alias of another
class proxy_csr_t: public csr_t {
 public:
  proxy_csr_t(processor_t* const proc, const reg_t addr, csr_t_p delegate);
  virtual reg_t read() const noexcept override;
 protected:
  bool unlogged_write(const reg_t val) noexcept override;
 private:
  csr_t_p delegate;
};

// For a CSR with a fixed, unchanging value
class const_csr_t: public csr_t {
 public:
  const_csr_t(processor_t* const proc, const reg_t addr, reg_t val);
  virtual reg_t read() const noexcept override;
 protected:
  bool unlogged_write(const reg_t val) noexcept override;
 private:
  const reg_t val;
};

// For a CSR that is an unprivileged accessor of a privileged counter
class counter_proxy_csr_t: public proxy_csr_t {
 public:
  counter_proxy_csr_t(processor_t* const proc, const reg_t addr, csr_t_p delegate);
  virtual void verify_permissions(insn_t insn, bool write) const override;
 private:
  bool myenable(csr_t_p counteren) const noexcept;
};

class mevent_csr_t: public basic_csr_t {
 public:
  mevent_csr_t(processor_t* const proc, const reg_t addr);
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

// For machine-level CSRs that only exist with Hypervisor
class hypervisor_csr_t: public basic_csr_t {
 public:
  hypervisor_csr_t(processor_t* const proc, const reg_t addr);
  virtual void verify_permissions(insn_t insn, bool write) const override;
};

class hideleg_csr_t: public masked_csr_t {
 public:
  hideleg_csr_t(processor_t* const proc, const reg_t addr, csr_t_p mideleg);
  virtual reg_t read() const noexcept override;
 private:
  csr_t_p mideleg;
};

class hgatp_csr_t: public basic_csr_t {
 public:
  hgatp_csr_t(processor_t* const proc, const reg_t addr);
  virtual void verify_permissions(insn_t insn, bool write) const override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

class tselect_csr_t: public basic_csr_t {
 public:
  tselect_csr_t(processor_t* const proc, const reg_t addr);
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

class tdata1_csr_t: public csr_t {
 public:
  tdata1_csr_t(processor_t* const proc, const reg_t addr);
  virtual reg_t read() const noexcept override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

class tdata2_csr_t: public csr_t {
 public:
  tdata2_csr_t(processor_t* const proc, const reg_t addr);
  virtual reg_t read() const noexcept override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

class tdata3_csr_t: public csr_t {
 public:
  tdata3_csr_t(processor_t* const proc, const reg_t addr);
  virtual reg_t read() const noexcept override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

class tinfo_csr_t: public csr_t {
 public:
  tinfo_csr_t(processor_t* const proc, const reg_t addr);
  virtual reg_t read() const noexcept override;
 protected:
  virtual bool unlogged_write(const reg_t UNUSED val) noexcept override { return false; };
};

// For CSRs that are only writable from debug mode
class debug_mode_csr_t: public basic_csr_t {
 public:
  debug_mode_csr_t(processor_t* const proc, const reg_t addr);
  virtual void verify_permissions(insn_t insn, bool write) const override;
};

class dpc_csr_t: public epc_csr_t {
 public:
  dpc_csr_t(processor_t* const proc, const reg_t addr);
  virtual void verify_permissions(insn_t insn, bool write) const override;
};

class dcsr_csr_t: public csr_t {
 public:
  dcsr_csr_t(processor_t* const proc, const reg_t addr);
  virtual void verify_permissions(insn_t insn, bool write) const override;
  virtual reg_t read() const noexcept override;
  void update_fields(const uint8_t cause, const uint8_t ext_cause, const reg_t prv,
                     const bool v, const elp_t pelp) noexcept;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 public:
  uint8_t prv;
  bool step;
  bool ebreakm;
  bool ebreaks;
  bool ebreaku;
  bool ebreakvs;
  bool ebreakvu;
  bool v;
  bool mprven;
  uint8_t cause;
  uint8_t ext_cause;
  bool cetrig;
  elp_t pelp;
};

typedef std::shared_ptr<dcsr_csr_t> dcsr_csr_t_p;

class float_csr_t final: public masked_csr_t {
 public:
  float_csr_t(processor_t* const proc, const reg_t addr, const reg_t mask, const reg_t init);
  virtual void verify_permissions(insn_t insn, bool write) const override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

typedef std::shared_ptr<float_csr_t> float_csr_t_p;

// For a CSR like FCSR, that is actually a view into multiple
// underlying registers.
class composite_csr_t: public csr_t {
 public:
  // We assume the lower_csr maps to bit 0.
  composite_csr_t(processor_t* const proc, const reg_t addr, csr_t_p upper_csr, csr_t_p lower_csr, const unsigned upper_lsb);
  virtual void verify_permissions(insn_t insn, bool write) const override;
  virtual reg_t read() const noexcept override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 private:
  csr_t_p upper_csr;
  csr_t_p lower_csr;
  const unsigned upper_lsb;
};

class seed_csr_t: public csr_t {
 public:
  seed_csr_t(processor_t* const proc, const reg_t addr);
  virtual void verify_permissions(insn_t insn, bool write) const override;
  virtual reg_t read() const noexcept override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

class vector_csr_t: public basic_csr_t {
 public:
  vector_csr_t(processor_t* const proc, const reg_t addr, const reg_t mask, const reg_t init=0);
  virtual void verify_permissions(insn_t insn, bool write) const override;
  // Write without regard to mask, and without touching mstatus.VS
  void write_raw(const reg_t val) noexcept;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 private:
  reg_t mask;
};

typedef std::shared_ptr<vector_csr_t> vector_csr_t_p;

// For CSRs shared between Vector and P extensions (vxsat)
class vxsat_csr_t: public masked_csr_t {
 public:
  vxsat_csr_t(processor_t* const proc, const reg_t addr);
  virtual void verify_permissions(insn_t insn, bool write) const override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

class hstateen_csr_t: public basic_csr_t {
 public:
  hstateen_csr_t(processor_t* const proc, const reg_t addr, const reg_t mask, const reg_t init, uint8_t index);
  virtual reg_t read() const noexcept override;
  virtual void verify_permissions(insn_t insn, bool write) const override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
protected:
  uint8_t index;
 private:
  const reg_t mask;
};

class sstateen_csr_t: public hstateen_csr_t {
 public:
  sstateen_csr_t(processor_t* const proc, const reg_t addr, const reg_t mask, const reg_t init, uint8_t index);
  virtual reg_t read() const noexcept override;
  virtual void verify_permissions(insn_t insn, bool write) const override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

class senvcfg_csr_t final: public envcfg_csr_t {
 public:
  senvcfg_csr_t(processor_t* const proc, const reg_t addr, const reg_t mask, const reg_t init);
  reg_t read() const noexcept override;
  virtual void verify_permissions(insn_t insn, bool write) const override;

 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

class stimecmp_csr_t: public basic_csr_t {
 public:
  stimecmp_csr_t(processor_t* const proc, const reg_t addr, const reg_t imask);
  virtual void verify_permissions(insn_t insn, bool write) const override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 private:
  reg_t intr_mask;
};

class virtualized_with_special_permission_csr_t: public virtualized_csr_t {
 public:
  virtualized_with_special_permission_csr_t(processor_t* const proc, csr_t_p orig, csr_t_p virt);
  virtual void verify_permissions(insn_t insn, bool write) const override;
};

class scountovf_csr_t: public csr_t {
 public:
  scountovf_csr_t(processor_t* const proc, const reg_t addr);
  virtual void verify_permissions(insn_t insn, bool write) const override;
  virtual reg_t read() const noexcept override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

class jvt_csr_t: public basic_csr_t {
 public:
  jvt_csr_t(processor_t* const proc, const reg_t addr, const reg_t init);
  virtual void verify_permissions(insn_t insn, bool write) const override;
};

// Sscsrind registers needs permissions checked
// (the original virtualized_csr_t does not call verify_permission of the underlying CSRs)
class virtualized_indirect_csr_t: public virtualized_csr_t {
 public:
  virtualized_indirect_csr_t(processor_t* const proc, csr_t_p orig, csr_t_p virt);
  virtual void verify_permissions(insn_t insn, bool write) const override;
};

class sscsrind_reg_csr_t : public csr_t {
 public:
  typedef std::shared_ptr<sscsrind_reg_csr_t> sscsrind_reg_csr_t_p;
  sscsrind_reg_csr_t(processor_t* const proc, const reg_t addr, csr_t_p iselect);
  reg_t read() const noexcept override;
  virtual void verify_permissions(insn_t insn, bool write) const override;
  void add_ireg_proxy(const reg_t iselect_val, csr_t_p proxy_csr);
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 private:
  csr_t_p iselect;
  std::unordered_map<reg_t, csr_t_p> ireg_proxy;
  csr_t_p get_reg() const noexcept;
};

// smcntrpmf_csr_t caches the previous state of the CSR in case a CSRW instruction
// modifies the state that should not be immediately visible to bump()
class smcntrpmf_csr_t : public masked_csr_t {
 public:
  smcntrpmf_csr_t(processor_t* const proc, const reg_t addr, const reg_t mask, const reg_t init);
  reg_t read_prev() const noexcept;
  void reset_prev() noexcept;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
 private:
  std::optional<reg_t> prev_val;
};

// srmcfg CSR provided by Ssqosid extension
class srmcfg_csr_t: public masked_csr_t {
 public:
  srmcfg_csr_t(processor_t* const proc, const reg_t addr, const reg_t mask, const reg_t init);
  virtual void verify_permissions(insn_t insn, bool write) const override;
};

class hvip_csr_t : public basic_csr_t {
 public:
  hvip_csr_t(processor_t* const proc, const reg_t addr, const reg_t init);
  reg_t read() const noexcept override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

typedef std::shared_ptr<hvip_csr_t> hvip_csr_t_p;

// ssp CSR provided by CFI Zicfiss extension
class ssp_csr_t final : public masked_csr_t {
 public:
  ssp_csr_t(processor_t* const proc, const reg_t addr, const reg_t mask, const reg_t init);
  virtual void verify_permissions(insn_t insn, bool write) const override;
};

// mtval2 CSR provided by H extension - but required if Ssdbltrp is  implemented
class mtval2_csr_t: public hypervisor_csr_t {
 public:
  mtval2_csr_t(processor_t* const proc, const reg_t addr);
  virtual void verify_permissions(insn_t insn, bool write) const override;
};

class hstatus_csr_t final: public basic_csr_t {
 public:
  hstatus_csr_t(processor_t* const proc, const reg_t addr);
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

class scntinhibit_csr_t: public basic_csr_t {
 public:
  scntinhibit_csr_t(processor_t* const proc, const reg_t addr, csr_t_p mcountinhibit);
  reg_t read() const noexcept override;
  virtual void verify_permissions(insn_t insn, bool write) const override;
 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

class mtopi_csr_t: public csr_t {
 public:
  mtopi_csr_t(processor_t* const proc, const reg_t addr);
  virtual reg_t read() const noexcept override;
 protected:
  bool unlogged_write(const reg_t val) noexcept override;
};

class mvip_csr_t : public basic_csr_t {
 public:
  mvip_csr_t(processor_t* const proc, const reg_t addr, const reg_t init);
  reg_t read() const noexcept override;

  void write_with_mask(const reg_t mask, const reg_t val) noexcept;

 protected:
  virtual bool unlogged_write(const reg_t val) noexcept override;
};

typedef std::shared_ptr<mvip_csr_t> mvip_csr_t_p;

class nonvirtual_stopi_csr_t: public csr_t {
 public:
  nonvirtual_stopi_csr_t(processor_t* const proc, const reg_t addr);
  virtual void verify_permissions(insn_t insn, bool write) const override;
  virtual reg_t read() const noexcept override;
 protected:
  bool unlogged_write(const reg_t val) noexcept override;
};

class inaccessible_csr_t: public csr_t {
 public:
  inaccessible_csr_t(processor_t* const proc, const reg_t addr);
  virtual void verify_permissions(insn_t insn, bool write) const override;
  reg_t read() const noexcept override { return 0; }
 protected:
  bool unlogged_write(const reg_t UNUSED val) noexcept override { return false; }
};

class vstopi_csr_t: public csr_t {
 public:
  vstopi_csr_t(processor_t* const proc, const reg_t addr);
  virtual void verify_permissions(insn_t insn, bool write) const override;
  virtual reg_t read() const noexcept override;
 protected:
  bool unlogged_write(const reg_t val) noexcept override;
};

class siselect_csr_t: public basic_csr_t {
 public:
  siselect_csr_t(processor_t* const proc, const reg_t addr, const reg_t init);
  virtual void verify_permissions(insn_t insn, bool write) const override;
};

class aia_csr_t: public masked_csr_t {
 public:
  aia_csr_t(processor_t* const proc, const reg_t addr, const reg_t mask, const reg_t init);
  virtual void verify_permissions(insn_t insn, bool write) const override;
};
#endif
