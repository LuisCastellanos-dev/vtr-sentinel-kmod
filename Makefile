# vtr-sentinel-kmod/Makefile
# FreeBSD kernel loadable module
# VTR-METH-001 v5.1 — Seguro por diseño
#
# Target: FreeBSD 14.x x86_64 (Pentium Silver)
# Portable: ARM Cortex-A53, RISC-V via vtr_arch.h
#
# Build:   make
# Install: make install (requiere root)
# Load:    kldload vtr_sentinel
# Unload:  kldunload vtr_sentinel
# Test:    make test

.PATH: ${.CURDIR}

# Nombre del módulo — debe coincidir con VTR_MODULE_NAME en vtr_sentinel.c
KMOD=   vtr_sentinel

# Fuentes del módulo
SRCS=   vtr_sentinel.c  \
        vtr_ring.c      \
        vtr_hooks.c

# Headers propios — incluidos en la verificación de dependencias
HDRS=   vtr_arch.h      \
        vtr_event.h     \
        vtr_ring.h

# Flags de compilación — más estrictos que el default del kernel
# -Werror: toda advertencia es error — sin compromisos
# -Wall:   todas las advertencias estándar
# -Wextra: advertencias adicionales
# -Wcast-align: detecta casts que violan alineación (crítico en ARM)
# -Wpointer-arith: aritmética de punteros a void es UB
# -Wmissing-prototypes: toda función debe tener prototipo declarado
CFLAGS+= -Wall
CFLAGS+= -Wextra
CFLAGS+= -Werror
CFLAGS+= -Wcast-align
CFLAGS+= -Wpointer-arith
CFLAGS+= -Wmissing-prototypes
CFLAGS+= -Wstrict-prototypes

# Verificación de tamaño de struct en compile-time
# _Static_assert está disponible en C11 — FreeBSD lo soporta
CFLAGS+= -std=c11

# Sin optimizaciones que oculten bugs en el hot path
# En release cambiar a -O2
CFLAGS+= -O1

# Incluir directorio local para vtr_arch.h, vtr_event.h, vtr_ring.h
CFLAGS+= -I${.CURDIR}

# Definición del módulo para el linker de FreeBSD
# Necesario para que kldload pueda identificar el módulo
SRCS+=  device_if.h bus_if.h

.include <bsd.kmod.mk>

# ── Targets adicionales ───────────────────────────────────────────────────

# Verificar que el sistema tiene los headers del kernel
check-headers:
	@echo "=== Verificando headers del kernel ==="
	@test -f /usr/src/sys/sys/module.h || \
		(echo "ERROR: /usr/src/sys no encontrado. Instalar src.txz" && exit 1)
	@test -f /usr/src/sys/sys/param.h || \
		(echo "ERROR: headers incompletos" && exit 1)
	@echo "OK: headers del kernel disponibles"
	@uname -r

# Verificar tamaño del módulo compilado
check-size: all
	@echo "=== Tamaño del módulo ==="
	@size vtr_sentinel.ko
	@echo "=== Símbolos exportados ==="
	@nm vtr_sentinel.ko | grep -v " [a-z] " | head -20

# Verificar que no hay símbolos undefined
check-symbols: all
	@echo "=== Verificando símbolos ==="
	@nm vtr_sentinel.ko | grep " U " && \
		echo "ADVERTENCIA: símbolos undefined" || \
		echo "OK: sin símbolos undefined"

# Cargar el módulo (requiere root)
load: all
	@echo "=== Cargando vtr_sentinel.ko ==="
	kldload ./vtr_sentinel.ko
	@echo "OK: módulo cargado"
	@kldstat | grep vtr_sentinel

# Descargar el módulo (requiere root)
unload:
	@echo "=== Descargando vtr_sentinel ==="
	kldunload vtr_sentinel
	@echo "OK: módulo descargado"

# Verificar que el cdev existe después de cargar
check-dev:
	@test -c /dev/vtr0 || (echo "ERROR: /dev/vtr0 no existe" && exit 1)
	@echo "OK: /dev/vtr0 existe"
	@ls -la /dev/vtr0

# Test mínimo — leer un EventRecord del cdev
# Requiere que el módulo esté cargado
test-read:
	@echo "=== Test de lectura /dev/vtr0 ==="
	@dd if=/dev/vtr0 bs=16 count=1 2>/dev/null | xxd | head -2 || \
		echo "ERROR: no se pudo leer de /dev/vtr0"

# Limpiar todo incluyendo archivos generados
clean-all: clean
	rm -f *.ko *.o *.kld device_if.h bus_if.h

# Pipeline completo de verificación
verify: check-headers all check-size check-symbols
	@echo ""
	@echo "=== vtr-sentinel-kmod verificación completa ==="
	@echo "Módulo: vtr_sentinel.ko"
	@echo "Target: FreeBSD $(shell uname -r)"
	@echo "Arch:   $(shell uname -m)"

.PHONY: check-headers check-size check-symbols load unload check-dev \
        test-read clean-all verify
