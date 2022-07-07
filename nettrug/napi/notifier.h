#ifndef BSTGW_NOTIFIER_H
#define BSTGW_NOTIFIER_H

#include "napi.h"

#include <kernel/spinlock.h>
#include <stdatomic.h>

struct notifier {
    atomic_flag cnt_flag;
    int check_period_ms;
    struct napi_dev_list *nl;

    int (*idle_func)(void *);
    void *worker_data;
};

void bstgw_init_notifier(struct notifier *n, int check_period_ms,
    int (*idle_function)(void *), void *worker_data);
void bstgw_notifier_loop(struct notifier *n, bool delay_idle_till_first_kick);
void bstgw_fini_notifier(struct notifier *n);

#endif /* BSTGW_NOTIFIER_H */
