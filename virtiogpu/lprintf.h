/*
For this to work, apply the following patch to qemu:

--- a/target/ppc/excp_helper.c
+++ b/target/ppc/excp_helper.c
@@ -115,4 +115,18 @@ static void dump_syscall(CPUPPCState *env)
                   ppc_dump_gpr(env, 4), ppc_dump_gpr(env, 5),
                   ppc_dump_gpr(env, 6), ppc_dump_gpr(env, 7),
                   ppc_dump_gpr(env, 8), env->nip);
+
+    if (ppc_dump_gpr(env, 3) == 0x113724FA &&
+        ppc_dump_gpr(env, 4) == 0x77810F9B &&
+        ppc_dump_gpr(env, 5) == 47) {
+
+        char cha = ppc_dump_gpr(env, 6);
+
+        if (cha == '\r' || cha == '\n') {
+            putchar('\n');
+            fflush(stdout);
+        } else {
+            putchar(cha);
+        }
+    };
 }
*/

#ifndef LPRINTF_H
#define LPRINTF_H

void lprintf(const char *fmt, ...);
void logTime(unsigned long letters, int andFlush);

#endif
