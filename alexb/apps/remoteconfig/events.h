#pragma once

#include <nul/compiler.h>
#include <nul/generic_service.h>

#include <util/capalloc_partition.h>

#define CONST_CAP_RANGE 16U

class EventService : public CapAllocatorAtomicPartition<1 << CONST_CAP_RANGE> 
{
private:

  struct ClientData : public GenericClientData {
  };

  ALIGNED(8) ClientDataStorage<ClientData, EventService> _storage;

  void check_clients(Utcb &utcb);

  bool enable_verbose;
  char * flag_revoke;

public:

  EventService() : CapAllocatorAtomicPartition<1 << CONST_CAP_RANGE>(1) {
    unsigned long long base = alloc_cap_region(1 << CONST_CAP_RANGE, 12);
    assert(base && !(base & 0xFFFULL));
    _cap_base = base;
  }


  inline unsigned alloc_cap(unsigned num = 1, unsigned cpu = ~0U) {
    unsigned cap, cap_last, first_cap;

    first_cap = CapAllocatorAtomicPartition::alloc_cap(1, cpu);
    if (!first_cap) return 0;
    cap_last = first_cap;

    while (--num) { //XXX fix me 
      cap = CapAllocatorAtomicPartition::alloc_cap(1, cpu);
      assert(cap);
      assert(cap_last + 1 == cap);
      cap_last = cap;
    } while (--num);

    return first_cap;
  }

  inline void dealloc_cap(unsigned cap, unsigned count = 1) {
    assert(count == 1); CapAllocatorAtomicPartition::dealloc_cap(cap, count);
  }

  inline unsigned alloc_crd() { return Crd(alloc_cap(), 0, DESC_CAP_ALL).value(); }

  unsigned portal_func(Utcb &utcb, Utcb::Frame &input, bool &free_cap);

  template <class C>
  bool start_service (Utcb *utcb, Hip * hip, C * c)
    {
      flag_revoke = new (0x1000) char[0x1000];
      if (!flag_revoke) return false;

      const char * service_name = "/events";
      unsigned res;
      unsigned exc_base_wo, exc_base_pf, pt_wo, pt_pf;
      unsigned service_cap = alloc_cap();
      Utcb *utcb_wo, *utcb_pf;
      
      for (unsigned cpunr = 0; cpunr < hip->cpu_desc_count(); cpunr++) {
        Hip_cpu const *cpu = &hip->cpus()[cpunr];
        if (not cpu->enabled()) continue;

        exc_base_wo = alloc_cap(16);
        exc_base_pf = alloc_cap(16);
        if (!exc_base_wo || !exc_base_pf) return false;
        pt_wo       = alloc_cap();
        pt_pf       = exc_base_wo + 0xe;

        unsigned cap_ec = c->create_ec_helper(this, cpunr, exc_base_wo, &utcb_wo, 0, alloc_cap());
        if (!cap_ec) return false;
        unsigned cap_pf = c->create_ec_helper(this, cpunr, exc_base_pf, &utcb_pf, 0, alloc_cap());
        if (!cap_pf) return false;

        utcb_wo->head.crd = alloc_crd();
        utcb_wo->head.crd_translate = Crd(_cap_base, CONST_CAP_RANGE, DESC_CAP_ALL).value();
        utcb_pf->head.crd = 0;

        unsigned long portal_func = reinterpret_cast<unsigned long>(StaticPortalFunc<EventService>::portal_func);
        res = nova_create_pt(pt_wo, cap_ec, portal_func, 0);
        if (res) return false;
        res = ParentProtocol::register_service(*utcb, service_name, cpunr, pt_wo, service_cap, flag_revoke);
        if (res) return !res;
      }

      return true;
    }
};