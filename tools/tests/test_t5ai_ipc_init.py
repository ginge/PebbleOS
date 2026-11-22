# SPDX-License-Identifier: Apache-2.0
"""Independent client init/retry without changing transport ownership or quarantine."""

import unittest

from tools.tests.t5ai_ipc_init_harness import run_init

ADC_FAILURE = r"""
 route_error[SARADC_NOTIFY_RX_CHANNEL]=E_ERROR;
 assert(t5ai_flash_ipc_init()==S_SUCCESS);
 assert(t5ai_flash_ipc_is_available() && connects==1);
 assert(!route_calls[SARADC_NOTIFY_RX_CHANNEL]);
 assert(t5ai_saradc_init()==E_ERROR);
 assert(t5ai_flash_ipc_init()==S_SUCCESS && connects==1);
 route_error[SARADC_NOTIFY_RX_CHANNEL]=S_SUCCESS;
 assert(t5ai_saradc_init()==S_SUCCESS);
 assert(t5ai_saradc_init()==S_SUCCESS && route_calls[SARADC_NOTIFY_RX_CHANNEL]==2);
 assert(route_calls[MB_IPC_RX_CHANNEL]==1 && sync_initializations==MB_IPC_SOCKET_COUNT);
"""


class IpcInitTest(unittest.TestCase):
    def test_adc_route_failure_and_retry_do_not_block_flash(self):
        run_init(ADC_FAILURE)

    def test_shared_transport_failure_retries_without_reinitializing_sync(self):
        run_init(r"""
 route_error[MB_IPC_RX_CHANNEL]=E_ERROR;
 assert(t5ai_flash_ipc_init()==E_ERROR && !connects);
 assert(t5ai_saradc_init()==E_ERROR && !route_calls[SARADC_NOTIFY_RX_CHANNEL]);
 assert(sync_initializations==MB_IPC_SOCKET_COUNT);
 route_error[MB_IPC_RX_CHANNEL]=S_SUCCESS;
 assert(t5ai_saradc_init()==S_SUCCESS);
 assert(t5ai_flash_ipc_init()==S_SUCCESS && connects==1);
 assert(sync_initializations==MB_IPC_SOCKET_COUNT && route_calls[MB_IPC_RX_CHANNEL]==3);
""")

    def test_definite_flash_connect_rejection_can_retry_without_touching_adc(self):
        run_init(r"""
 reject_connect=true;
 assert(t5ai_flash_ipc_init()==E_ERROR && !t5ai_flash_ipc_is_available());
 assert(t5ai_saradc_init()==S_SUCCESS);
 reject_connect=false;
 assert(t5ai_flash_ipc_init()==S_SUCCESS && connects==2);
 for(unsigned n=0;n<10;++n)assert(t5ai_flash_ipc_init()==S_SUCCESS);
 assert(connects==2 && route_calls[SARADC_NOTIFY_RX_CHANNEL]==1);
""")

    def test_ambiguous_flash_connect_stays_quarantined_while_adc_can_start(self):
        run_init(r"""
 drop_response=true;
 assert(t5ai_flash_ipc_init()==E_AGAIN && !t5ai_flash_ipc_is_available());
 assert(t5ai_flash_ipc_init()==E_INVALID_OPERATION && connects==1);
 assert(t5ai_saradc_request(15,16,adc_completed,NULL)==S_SUCCESS);
 assert(s_socket && !s_socket->poisoned && s_active);
""")

    def test_success_and_later_flash_quarantine_are_both_idempotent(self):
        run_init(r"""
 assert(t5ai_flash_ipc_init()==S_SUCCESS);
 for(unsigned n=0;n<10;++n)assert(t5ai_flash_ipc_init()==S_SUCCESS);
 assert(connects==1 && !route_calls[SARADC_NOTIFY_RX_CHANNEL]);
 prv_flash_quarantine();assert(sleep_faults==1 && s_flash_socket->poisoned);
 for(unsigned n=0;n<10;++n)assert(t5ai_flash_ipc_init()==E_INVALID_OPERATION);
 assert(!t5ai_flash_ipc_is_available() && connects==1);
 assert(t5ai_saradc_request(15,16,adc_completed,NULL)==S_SUCCESS);
 assert(!s_socket->poisoned);
""")

    def test_either_socket_allocation_order_and_adc_timeout_preserve_flash(self):
        for adc_first in (False, True):
            with self.subTest(adc_first=adc_first):
                run_init(
                    (
                        r"""
 assert(t5ai_saradc_request(15,16,adc_completed,NULL)==S_SUCCESS);
 assert(t5ai_flash_ipc_init()==S_SUCCESS);
"""
                        if adc_first
                        else r"""
 assert(t5ai_flash_ipc_init()==S_SUCCESS);
 assert(t5ai_saradc_request(15,16,adc_completed,NULL)==S_SUCCESS);
"""
                    )
                    + r"""
 assert(s_flash_socket!=s_socket && s_flash_socket->client_port==16 && s_socket->client_port==17);
 assert(s_flash_socket->server_port==1 && s_socket->server_port==2);
 assert(s_flash_socket==&s_sockets["""
                    + str(int(adc_first))
                    + r"""]);
 now+=SARADC_CONNECT_TIMEOUT_MS;t5ai_mb_ipc_async_poll();
 assert(completed==1 && completed_status==E_AGAIN && !s_active && !s_available);
 assert(t5ai_saradc_request(15,16,adc_completed,NULL)==E_INVALID_OPERATION);
 assert(t5ai_flash_ipc_init()==S_SUCCESS && connects==1 && !s_flash_socket->poisoned);
"""
                )
