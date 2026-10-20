/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Defiant Web.
 *
 * Ghoti.io Defiant Web is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License version
 * 3 as published by the Free Software Foundation.
 *
 * Ghoti.io Defiant Web is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * The `defiant` program: one installation, one listener. Settings come from
 * the environment.
 *
 *   DEFIANT_LISTEN, DEFIANT_TRUSTED_PROXIES, DEFIANT_SECRET,
 *   DEFIANT_SECRET_FILE, DEFIANT_CONFIG   read by defiant-core
 *   DEFIANT_DATA            directory for the SQLite files; empty means the
 *                           memory backend and nothing is kept
 *   DEFIANT_ADMIN_USER      the first admin, used only when the default
 *   DEFIANT_ADMIN_PASSWORD  tenant has no account
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <ghoti.io/defiant-web/defiant-web.h>
#include <ghoti.io/defiant-core/access.h>
#include <ghoti.io/defiant-core/account.h>
#include <ghoti.io/defiant-core/entity.h>
#include <ghoti.io/defiant-core/inst.h>
#include <ghoti.io/defiant-core/render.h>
#include <ghoti.io/defiant-core/subject.h>
#include <ghoti.io/defiant-storage-memory/memory.h>
#include <ghoti.io/defiant-storage-sqlite/model.h>
#include <ghoti.io/security/random.h>
#include <ghoti.io/cutil/fiber.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#define make_dir(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define make_dir(path) mkdir((path), 0700)
#endif

/** How long a stop waits for a request that is being served. */
#define kStopWait 5

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
  (void)sig;
  g_stop = 1;
}

static void wipe(void * p, size_t n) {
  volatile unsigned char * b = (volatile unsigned char *)p;
  size_t i;
  for (i = 0; i < n; i++) {
    b[i] = 0;
  }
}

static const char * env(const char * name) {
  const char * v = getenv(name);
  return v != NULL && v[0] != '\0' ? v : NULL;
}

/* Remove a secret from the environment once it has been read. */
static void forget(const char * name) {
  char blank[160];
  snprintf(blank, sizeof blank, "%s=", name);
#ifdef _WIN32
  _putenv(blank);
#else
  unsetenv(name);
#endif
}

static int ensure_dir(const char * path) {
  if (make_dir(path) != 0 && errno != EEXIST) {
    fprintf(stderr, "defiant: cannot create %s: %s\n", path, strerror(errno));
    return -1;
  }
  return 0;
}

static int open_store(const char * data, const char * name, GDEFSTOR_Store ** out) {
  if (data == NULL) {
    if (gdefsmemory_store_create(NULL, NULL, out) != GDEFSMEMORY_OK) {
      fprintf(stderr, "defiant: cannot open the memory store\n");
      return -1;
    }
    return 0;
  }
  {
    char path[1024];
    if (snprintf(path, sizeof path, "%s/%s", data, name) >= (int)sizeof path ||
        ensure_dir(path) != 0) {
      return -1;
    }
    /* The entity port's store (story S6b): the site's entities and the
     * installation's records are the port's. */
    if (gdefssqlite_model_store_create(path, NULL, NULL, NULL, out) !=
        GDEFSSQLITE_OK) {
      fprintf(stderr, "defiant: cannot open the SQLite store in %s\n", path);
      return -1;
    }
  }
  return 0;
}

/* The key store (AD-19): the tenant keyrings, and later subject keys and the
 * ledger, apart from every data store. In memory without DEFIANT_DATA, else a
 * directory of key files of its own. */
static int open_keys(const char * data, GDEFSTOR_KeyStore ** out) {
  static GDEFSTOR_KeyCipher cipher;
  gdefcore_subject_cipher(&cipher);
  if (data == NULL) {
    if (gdefsmemory_keystore_create(NULL, &cipher, out) != GDEFSMEMORY_OK) {
      fprintf(stderr, "defiant: cannot open the memory key store\n");
      return -1;
    }
    return 0;
  }
  {
    char path[1024];
    if (snprintf(path, sizeof path, "%s/keys", data) >= (int)sizeof path ||
        ensure_dir(path) != 0) {
      return -1;
    }
    if (gdefssqlite_keystore_create(path, &cipher, NULL, NULL, out) !=
        GDEFSSQLITE_OK) {
      fprintf(stderr, "defiant: cannot open the key store in %s\n", path);
      return -1;
    }
  }
  return 0;
}

/* The first admin, only when the tenant has no account at all. */
static int first_admin(GDEFCORE_Inst * inst, const char * id, const uint8_t * root) {
  GDEFCORE_Catalog * catalog = NULL;
  GDEFCORE_Keyring * keyring;
  char ** accounts = NULL;
  size_t count = 0;
  const char * user = env("DEFIANT_ADMIN_USER");
  const char * pass = env("DEFIANT_ADMIN_PASSWORD");
  int rc = -1;
  if (gdefcore_inst_catalog_open(inst, id, strlen(id), &catalog) != GDEFCORE_OK) {
    fprintf(stderr, "defiant: cannot open the default tenant\n");
    return -1;
  }
  keyring = gdefcore_inst_tenant_keyring(inst, id, strlen(id));
  if (gdefcore_entity_ids(catalog, "core.account", 12, &accounts, &count) !=
      GDEFCORE_OK) {
    accounts = NULL;
    count = 0;
  } else {
    gdefcore_ids_free(catalog, accounts, count);
  }
  if (count > 0) {
    rc = 0;
  } else if (user == NULL || pass == NULL) {
    fprintf(stderr,
        "defiant: the tenant has no account; set DEFIANT_ADMIN_USER and "
        "DEFIANT_ADMIN_PASSWORD for the first start\n");
  } else {
    char admin[GDEFCORE_PUBLIC_ID_LEN + 1];
    char account[GDEFCORE_PUBLIC_ID_LEN + 1];
    if (keyring != NULL &&
        gdefcore_access_admin_principal(catalog, admin) == GDEFCORE_OK &&
        gdefcore_account_bind(catalog, keyring, root, GDEFCORE_ACCOUNT_ROOT_LEN,
            admin, strlen(admin), user, strlen(user), pass, strlen(pass),
            account) == GDEFCORE_OK) {
      fprintf(stderr, "defiant: created the first admin account\n");
      rc = 0;
    } else {
      fprintf(stderr, "defiant: could not create the admin account\n");
    }
  }
  gdefcore_catalog_destroy(catalog);
  return rc;
}

int main(void) {
  GDEFCORE_Inst_Config * config = NULL;
  GDEFSTOR_Store * registry = NULL;
  GDEFSTOR_Store * site = NULL;
  GDEFSTOR_KeyStore * keys = NULL;
  GDEFCORE_Inst * inst = NULL;
  GDEFWEB_Server * server = NULL;
  GDEFCORE_Catalog * catalog = NULL;
  uint8_t root[GDEFCORE_ACCOUNT_ROOT_LEN];
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  const char * data = env("DEFIANT_DATA");
  GCU_Socket_Address local;
  uint8_t * kit = NULL;
  size_t kit_n = 0;
  int status = 2;
  int failures = 0;
  time_t stop_at = 0;

  if (gdefcore_inst_config_load(NULL, &config) != GDEFCORE_OK) {
    fprintf(stderr, "defiant: a setting is not valid\n");
    return 2;
  }
  if (gdefcore_inst_config_root(config, root) != GDEFCORE_OK) {
    if (data != NULL) {
      fprintf(stderr,
          "defiant: DEFIANT_DATA is set, so DEFIANT_SECRET or DEFIANT_SECRET_FILE "
          "is needed; a random key would lock this data after a restart\n");
      gdefcore_inst_config_destroy(config);
      return 2;
    }
    if (gsec_random_bytes(root, sizeof root, NULL) != GSEC_OK) {
      fprintf(stderr, "defiant: no random bytes\n");
      gdefcore_inst_config_destroy(config);
      return 2;
    }
  }
  if (data != NULL && ensure_dir(data) != 0) {
    goto done;
  }
  if (open_store(data, "registry", &registry) != 0 ||
      open_store(data, "site", &site) != 0 || open_keys(data, &keys) != 0) {
    goto done;
  }
  if (gdefcore_inst_open_keys(registry, site, keys, root, sizeof root, "", 0, NULL,
          &inst) != GDEFCORE_OK ||
      gdefcore_inst_default(inst, id) != GDEFCORE_OK ||
      gdefcore_inst_catalog_open(inst, id, strlen(id), &catalog) != GDEFCORE_OK ||
      gdefcore_render_kit(catalog, &kit, &kit_n) != GDEFCORE_OK ||
      gdefcore_render_install(catalog, kit, kit_n) != GDEFCORE_OK) {
    fprintf(stderr, "defiant: cannot open the installation\n");
    goto done;
  }
  gdefcore_text_free(catalog, (char *)kit);
  gdefcore_catalog_destroy(catalog);
  catalog = NULL;
  if (first_admin(inst, id, root) != 0) {
    goto done;
  }
  forget("DEFIANT_ADMIN_PASSWORD");
  if (gdefweb_server_create(config, NULL, &server) != GDEFWEB_OK ||
      gdefweb_server_set_inst(server, inst, root, sizeof root) != GDEFWEB_OK ||
      gdefweb_server_listen(server) != GDEFWEB_OK) {
    fprintf(stderr, "defiant: cannot start the listener\n");
    goto done;
  }
  gdefcore_inst_config_destroy(config);
  config = NULL;
  if (!gdefweb_server_accepting(server)) {
    fprintf(stderr, "defiant: DEFIANT_LISTEN is empty, so nothing is bound\n");
    goto done;
  }
  if (data == NULL) {
    fprintf(stderr, "defiant: memory backend; nothing is kept when this stops\n");
  }
  if (gdefweb_server_local(server, &local) == GDEFWEB_OK) {
    fprintf(stderr, "defiant: listening on port %u\n", (unsigned)local.port);
    fflush(stderr);
  }
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  for (;;) {
    if (g_stop) {
      if (stop_at == 0) {
        stop_at = time(NULL);
      }
      if (!gdefweb_server_serving(server) || time(NULL) - stop_at >= kStopWait) {
        break;
      }
    } else {
      GDEFWEB_Result rc = gdefweb_server_accept_one(server);
      if (rc != GDEFWEB_OK && rc != GDEFWEB_ERR_BUSY) {
        fprintf(stderr, "defiant: cannot accept a connection\n");
        status = 1;
        goto done;
      }
    }
    if (gcu_loop_run_once(gdefweb_server_loop(server), 100) != GCU_LOOP_OK) {
      if (++failures > 1000) {
        fprintf(stderr, "defiant: the event loop keeps failing\n");
        status = 1;
        goto done;
      }
    } else {
      failures = 0;
    }
  }
  status = 0;
done:
  gdefweb_server_destroy(server);
  if (catalog != NULL) {
    gdefcore_catalog_destroy(catalog);
  }
  gdefcore_inst_destroy(inst);
  if (keys != NULL) {
    gdefstor_keystore_destroy(keys);
  }
  if (site != NULL) {
    gdefstor_store_destroy(site);
  }
  if (registry != NULL) {
    gdefstor_store_destroy(registry);
  }
  if (config != NULL) {
    gdefcore_inst_config_destroy(config);
  }
  wipe(root, sizeof root);
  return status;
}
