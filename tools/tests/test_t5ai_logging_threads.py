# SPDX-License-Identifier: Apache-2.0
"""Native identities, recursion, boot phases and mailbox-owner exclusion."""

import unittest

from tools.tests.t5ai_logging_harness import run_logging

DISTINCT_THREADS = r"""
 setup();
 LogState *state=kernel_applib_get_log_state();assert(state);
 strcpy(state->buffer,"main");
 assert(!kernel_applib_get_log_state() && attempts==1);
 pthread_t other;assert(!pthread_create(&other,NULL,worker,NULL));
 assert(!pthread_mutex_lock(&gate));
 struct timespec end=deadline();
 while(attempts<2 && !worker_done)
   assert(!pthread_cond_timedwait(&changed,&gate,&end));
 assert(!worker_done && attempts==2); // A distinct Unknown thread waits, not recursion.
 assert(!strcmp(state->buffer,"main"));
 assert(!pthread_mutex_unlock(&gate));
 kernel_applib_release_log_state(state);
 assert(!pthread_join(other,NULL));
 assert(worker_done && worker_obtained && attempts==2 && unlocks==2);
 state=kernel_applib_get_log_state();assert(state);
 kernel_applib_release_log_state(state);
"""


class LoggingThreadsTest(unittest.TestCase):
    def test_distinct_unknown_threads_serialize_instead_of_false_recursion(self):
        run_logging(DISTINCT_THREADS)

    def test_timeout_does_not_poison_a_native_threads_next_log(self):
        run_logging(r"""
 setup();LogState *state=kernel_applib_get_log_state();assert(state);
 pthread_t other;assert(!pthread_create(&other,NULL,worker,(void *)&main_thread));
 assert(!pthread_mutex_lock(&gate));struct timespec end=deadline();
 while(!worker_done)assert(!pthread_cond_timedwait(&changed,&gate,&end));
 assert(!worker_obtained && attempts==2 && unlocks==0);
 assert(!pthread_mutex_unlock(&gate));
 kernel_applib_release_log_state(state);
 assert(!pthread_mutex_lock(&gate));retry_allowed=true;
 assert(!pthread_cond_broadcast(&changed));assert(!pthread_mutex_unlock(&gate));
 assert(!pthread_join(other,NULL));assert(attempts==3 && unlocks==2);
""")

    def test_recursive_lock_failure_and_release_do_not_reenter(self):
        run_logging(r"""
 setup();pbl_thread_tls_set(&self,1,&main_thread);
 recurse_lock=fail_lock=true;
 assert(!kernel_applib_get_log_state() && attempts==1 && !unlocks);
 assert(!pbl_thread_tls_get(&self,LOG_ENTERED_TLS_SLOT));
 fail_lock=false;recurse_lock=recurse_unlock=true;
 LogState *state=kernel_applib_get_log_state();assert(state && attempts==2);
 assert(pbl_thread_tls_get(&self,LOG_ENTERED_TLS_SLOT));
 kernel_applib_release_log_state(state);
 assert(attempts==2 && unlocks==1 && !pbl_thread_tls_get(&self,LOG_ENTERED_TLS_SLOT));
 assert(pbl_thread_tls_get(&self,1)==&main_thread);
""")

    def test_pre_scheduler_boot_and_pre_applib_window(self):
        run_logging(r"""
 LogState *state=kernel_applib_get_log_state();assert(state && !attempts && !current_calls);
 assert(!kernel_applib_get_log_state());kernel_applib_release_log_state(state);
 state=kernel_applib_get_log_state();assert(state);kernel_applib_release_log_state(state);
 assert(!current_calls && !unlocks && !irq_depth);
 started=running=registered=true;
 assert(!kernel_applib_get_log_state() && !attempts);
 assert(!pbl_thread_tls_get(&self,LOG_ENTERED_TLS_SLOT));
 kernel_applib_init();assert(service_inits==2);
 state=kernel_applib_get_log_state();assert(state && attempts==1);
 kernel_applib_release_log_state(state);assert(unlocks==1);
""")

    def test_isr_critical_and_scheduler_locked_paths_never_wait(self):
        run_logging(r"""
 setup();
 for(unsigned mode=0;mode<3;++mode) {
   if(mode==0)isr=true;
   if(mode==1)pbl_irq_lock();
   if(mode==2)running=false;
   LogState *state=kernel_applib_get_log_state();assert(state && !attempts);
   assert(pbl_thread_tls_get(&self,LOG_ENTERED_TLS_SLOT));
   assert(!kernel_applib_get_log_state());kernel_applib_release_log_state(state);
   assert(!pbl_thread_tls_get(&self,LOG_ENTERED_TLS_SLOT));
   if(mode==0)isr=false;
   if(mode==1)pbl_irq_unlock();
   if(mode==2)running=true;
 }
 assert(!unlocks && !irq_depth);
 LogState *state=kernel_applib_get_log_state();assert(state && attempts==1);
 isr=true;assert(!kernel_applib_get_log_state());isr=false;
 running=false;assert(!kernel_applib_get_log_state());running=true;
 kernel_applib_release_log_state(state);assert(unlocks==1);
""")

    def test_nonblocking_context_cannot_overwrite_another_threads_buffer(self):
        run_logging(r"""
 setup();struct pbl_thread first={0};
 LogState *state=kernel_applib_get_log_state();assert(state);strcpy(state->buffer,"held");
 // Model a different scheduled identity entering a critical section.
 first=self;memset(&self,0,sizeof(self));pbl_irq_lock();
 assert(!kernel_applib_get_log_state() && attempts==1);
 assert(!pbl_thread_tls_get(&self,LOG_ENTERED_TLS_SLOT));
 assert(!strcmp(state->buffer,"held"));pbl_irq_unlock();self=first;
 kernel_applib_release_log_state(state);assert(unlocks==1);
""")

    def test_mailbox_owner_is_excluded_before_state_and_locks(self):
        run_logging(r"""
 owner=true;assert(!kernel_applib_get_log_state());
 assert(!attempts && !current_calls && !irq_depth);
 owner=false;setup();LogState *state=kernel_applib_get_log_state();assert(state);
 void *entered=pbl_thread_tls_get(&self,LOG_ENTERED_TLS_SLOT);
 owner=true;assert(!kernel_applib_get_log_state());owner=false;
 assert(attempts==1 && pbl_thread_tls_get(&self,LOG_ENTERED_TLS_SLOT)==entered);
 kernel_applib_release_log_state(state);
""")

    def test_release_uses_original_admission_when_init_becomes_ready(self):
        run_logging(r"""
 started=running=true;
 LogState *state=kernel_applib_get_log_state();assert(state && !attempts);
 kernel_applib_init();registered=true;
 kernel_applib_release_log_state(state);assert(!unlocks);
 state=kernel_applib_get_log_state();assert(state && attempts==1);
 kernel_applib_release_log_state(state);assert(unlocks==1);
""")
