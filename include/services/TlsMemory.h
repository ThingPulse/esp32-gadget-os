#pragma once

// Call once at boot, before services can create TLS clients.
void configure_tls_memory();
