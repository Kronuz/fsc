--- fscd/fscd.c.orig	2013-06-03 14:03:41 UTC
+++ fscd/fscd.c
@@ -82,8 +82,7 @@
 #define CONF_FILE SYSCONFDIR"fscd.conf"
 #define SERVICE "service"
 #define STATUS "onestatus"
-#define START "onestart"
-#define RESTART "onerestart" // restart is more reliable than just start.
+#define START "restart" // restart is more reliable than just start.
 #define VERSION "1.1"
 
 struct spid {
@@ -109,6 +108,10 @@
 	int kq;
 };
 
+#if defined(__FreeBSD__)
+	static struct pidfh *pfh = NULL;
+#endif
+
 static int debug = 0;
 static char *socketname = NULL;
 static char *conffile = NULL;
@@ -147,10 +150,6 @@
 	struct stat nb_stat;
 	char errorstr[LINE_MAX];
 
-#if defined(__FreeBSD__)
-	struct pidfh *pfh;
-#endif
-
 	/* check arguments */
 	while ((ch = getopt(argc, argv, "Vdvfs:c:")) != -1)
 		switch (ch) {
@@ -301,7 +300,7 @@
 }
 
 /*
- * Determine the status of the exited process. If it is a signal which is likely 
+ * Determine the status of the exited process. If it is a signal which is likely
  * to be user-issued, return 0, 1 otherwise.
  */
 static int
@@ -325,6 +324,7 @@
 static int
 handle_restart(struct fscd_cfg *config, char *sname)
 {
+	int ret;
 	struct service *svs;
 	struct spid *svpid;
 
@@ -338,12 +338,15 @@
 			free(svpid);
 		}
 
-		if (start_service(svs)) {
-			printlog(LOG_ERR, "Could not restart service.");
-			return -1;
-		} else if (fill_pids(svs)) {
+		if ((ret=start_service(svs))) {
+			if (ret < 0)
+				printlog(LOG_ERR, "%s could not be restarted.", svs->svname);
+			else
+				printlog(LOG_ERR, "%s could not be restarted. Set %s_enable to YES in %cetc/rc.conf.", svs->svname, svs->svname, '/');
+			return ret;
+		} else if ((ret=fill_pids(svs))) {
 			printlog(LOG_ERR, "Could not get pids for service.");
-			return -1;
+			return ret;
 		} else if (kqueue_service(config, svs)) {
 			printlog(LOG_ERR, "Could not monitor service.");
 			return -1;
@@ -399,7 +402,7 @@
 
 			/* Wait for 100 seconds for the service to restart. */
 			pthread_mutex_lock(&inputv->config->service_mtx);
-			if (fill_pids(svs) == 0) {
+			if (!fill_pids(svs)) {
 				if (kqueue_service(inputv->config, svs))
 					printlog(LOG_ERR, "Could not monitor service.");
 				else
@@ -418,13 +421,13 @@
 		}
 		sleep(10);
 	}
-	printlog(LOG_ERR, "Service %s was not restarted. Doing it myself.", 
+	printlog(LOG_ERR, "Service %s was not restarted. Doing it myself.",
 	    inputv->sname);
 	pthread_mutex_lock(&inputv->config->service_mtx);
 	handle_restart(inputv->config, inputv->sname);
 	pthread_mutex_unlock(&inputv->config->service_mtx);
 	return NULL;
-	
+
 	printlog(LOG_ERR, "Service %s should be waited for, but was not found.",
 	    inputv->sname);
 	return NULL;
@@ -441,6 +444,9 @@
 	char *statstream;
 	char errorstr[LINE_MAX];
 	char eot = 4;
+	char status = 0;
+
+	send(sock_fd, &status, 1, 0);
 
 	/* Our own pid. */
 	if (asprintf(&statstream, "The fscd pid is %d.\n", getpid()) <= 0) {
@@ -567,10 +573,10 @@
 
 	retcode = system(cmdstr);
 	free(cmdstr);
-	if (WEXITSTATUS(retcode) == 0)
-		return 1;
-	else
+	if (WEXITSTATUS(retcode))
 		return 0;
+	else
+		return 1;
 }
 
 /*
@@ -639,6 +645,8 @@
 	char pinput[LINE_MAX];
 	char errorstr[LINE_MAX];
 	FILE *pp;
+	int retcode;
+	pid_t pid;
 
 	/* Empty list. */
 	if (!SLIST_EMPTY(&svs->svpids)) {
@@ -666,49 +674,39 @@
 		return -1;
 	}
 
-	if (fgets(pinput, sizeof pinput, pp) == NULL) {
-		if (strerror_r(errno, errorstr, sizeof errorstr))
-			printlog(LOG_ERR, "fgets failed: %s", svs->svname, errorstr);
-		else
-			printlog(LOG_ERR, "fgets failed.", svs->svname);
-		pclose(pp);
-		return -1;
-	}
-	pclose(pp);
+	while (fgets(pinput, sizeof pinput, pp) != NULL) {
+		if (strstr(pinput, "_enable to YES in") != NULL) {
+			pclose(pp);
+			return 1;
+		}
 
-	/* Scan the output. We want (see /etc/rc.subr):
-	 *   ${name} is running as pid $rc_pid.
-	 * or
-	 *   ${name} is not running.
-	 * with $rc_pid being a space-separated list of pids.
-	 * We cannot scan for the service's name, as the name might be different
-	 * to the service script name.
-	 * Though we could assume the service name is properly set in its rc script 
-	 * and we could thus just parse the script ourselves, exceptions here might 
-	 * have the same probability as services with different service and script 
-	 * names.
-	 * So we have to skip the first portion up to the "is not running" or "is 
-	 * runnind as pid" and assume service(8) returns the right script's output.
-	 */
-	if ((pinputp = strstr(pinput, " is not running.")) != NULL) {
-		return 1;
-	} else if ((pinputp = strstr(pinput, " is running as pid ")) == NULL) {
-		printlog(LOG_ERR, "Could not parse output from `service %s status`. Cause is either a non-standard rc script or (very unlikely) an incompatible rc.subr version.", svs->svname);
-		return -1;
+		/* Scan the output for a separated list of numbers assuming they are pids.
+		 * We cannot scan for the service's name, as the name might be different
+		 * to the service script name.
+		 * Though we could assume the service name is properly set in its rc script
+		 * and we could thus just parse the script ourselves, exceptions here might
+		 * have the same probability as services with different service and script
+		 * names.
+		 * So we have to skip the first portion up to the "is not running" or "is
+		 * runnind as pid" and assume service(8) returns the right script's output.
+		 */
+		for (pinputp = strtok_r(pinput, " .,\t\n", &ttmpstr);
+				pinputp;
+				pinputp = strtok_r(NULL, " .,\t\n", &ttmpstr)) {
+			pid = strtoul(pinputp, &tmpstr, 10);
+			if (tmpstr && tmpstr > pinputp) {
+				if (pid > 0 && getsid(pid) != -1) {
+					svpid = malloc(sizeof(struct spid));
+					svpid->svpid = pid;
+					SLIST_INSERT_HEAD(&svs->svpids, svpid, next);
+				}
+			}
+		}
 	}
-	pinputp = pinputp + 19;
+	retcode = pclose(pp);
 
-	for (pinputp = strtok_r(pinputp, " .\n", &ttmpstr);
-			pinputp;
-			pinputp = strtok_r(NULL, " .\n", &ttmpstr)) {
-		svpid = malloc(sizeof(struct spid));
-		svpid->svpid = strtoul(pinputp, &tmpstr, 10);
-		if ((tmpstr && tmpstr[0]) || svpid->svpid <= 0) {
-			printlog(LOG_ERR, "Invalid output from rc.subr. Could not get all pids.");
-			free(svpid);
-			return -1;
-		}
-		SLIST_INSERT_HEAD(&svs->svpids, svpid, next);
+	if (WEXITSTATUS(retcode)) {
+		return -1;
 	}
 
 	return 0;
@@ -725,19 +723,38 @@
 	int ret;
 	int retcode;
 	char *cmdstr;
+	char pinput[LINE_MAX];
+	FILE *pp;
 
-	if (asprintf(&cmdstr, SERVICE " %s " RESTART, svs->svname) <= 0) {
+	if (asprintf(&cmdstr, SERVICE " %s " START, svs->svname) <= 0) {
 		if (strerror_r(errno, errorstr, sizeof errorstr))
 			printlog(LOG_ERR, "asprintf for executing %s failed: %s", svs->svname, errorstr);
 		else
 			printlog(LOG_ERR, "asprintf for executing %s failed.", svs->svname);
-		return 0;
+		return -1;
 	}
 
-	retcode = system(cmdstr);
+	pp = popen(cmdstr, "r");
 	free(cmdstr);
-	if (WEXITSTATUS(retcode))
+	if (pp == NULL) {
+		if (strerror_r(errno, errorstr, sizeof errorstr))
+			printlog(LOG_ERR, "popen failed: %s", svs->svname, errorstr);
+		else
+			printlog(LOG_ERR, "popen failed.", svs->svname);
+		return -1;
+	}
+
+	while (fgets(pinput, sizeof pinput, pp) != NULL) {
+		if (strstr(pinput, "_enable to YES in") != NULL) {
+			pclose(pp);
+			return 1;
+		}
+	}
+	retcode = pclose(pp);
+
+	if (WEXITSTATUS(retcode)) {
 		return -1;
+	}
 
 	/* Refresh our stored pid and re-register with kqueue. */
 	ret = -1;
@@ -785,14 +802,15 @@
 static int
 register_service(struct fscd_cfg *config, struct service *svs)
 {
+	int ret;
 	char errorstr[LINE_MAX];
 
-	if (SLIST_EMPTY(&svs->svpids) && fill_pids(svs)) {
+	if (SLIST_EMPTY(&svs->svpids) && (ret=fill_pids(svs))) {
 		if (strerror_r(errno, errorstr, sizeof errorstr))
 			printlog(LOG_ERR, "Getting pids failed");
 		else
 			printlog(LOG_ERR, "Getting pids failed: %s", errorstr);
-		return -1;
+		return ret;
 	}
 
 	if (kqueue_service(config, svs))
@@ -869,10 +887,12 @@
 				if (!svs) {
 					printlog(LOG_ERR, "%s could not be built a structure for.", svs->svname);
 					ret = -1;
-				} else if (register_service(config, svs)) {
-					printlog(LOG_ERR, "%s could not be monitored.", svs->svname);
+				} else if ((ret=register_service(config, svs))) {
+					if (ret < 0)
+						printlog(LOG_ERR, "%s could not be monitored.", svs->svname);
+					else
+						printlog(LOG_ERR, "%s could not be monitored. Set %s_enable to YES in %cetc/rc.conf.", svs->svname, svs->svname, '/');
 					free(svs);
-					ret = -1;
 				}
 			}
 		} else {
@@ -886,14 +906,18 @@
 				if (!svs) {
 					printlog(LOG_ERR, "%s could not be built a structure for.", svs->svname);
 					ret = -1;
-				} else if (start_service(svs)) {
-					printlog(LOG_ERR, "%s could not be started.", svs->svname);
+				} else if ((ret=start_service(svs))) {
+					if (ret < 0)
+						printlog(LOG_ERR, "%s could not be started.", svs->svname);
+					else
+						printlog(LOG_ERR, "%s could not be started. Set %s_enable to YES in %cetc/rc.conf.", svs->svname, svs->svname, '/');
 					free(svs);
-					ret = -1;
-				} else if (register_service(config, svs)) {
-					printlog(LOG_ERR, "%s could not be monitored.", svs->svname);
+				} else if ((ret=register_service(config, svs))) {
+					if (ret < 0)
+						printlog(LOG_ERR, "%s could not be monitored.", svs->svname);
+					else
+						printlog(LOG_ERR, "%s could not be monitored. Set %s_enable to YES in %cetc/rc.conf.", svs->svname, svs->svname, '/');
 					free(svs);
-					ret = -1;
 				} else {
 					printlog(LOG_INFO, "%s started from config file.", svs->svname);
 				}
@@ -1023,6 +1047,7 @@
 	char *sendstr;
 	struct service *svs;
 	char eot = 4;
+	char status =0;
 
 	for (iter = arglst; (*iter = strsep(&serviceline, ":")) != NULL;) {
 		if (**iter != '\0')
@@ -1031,53 +1056,72 @@
 	}
 
 	pthread_mutex_lock(&config->service_mtx);
+
 	/* enable */
 	if (strcmp(arglst[0], "enable") == 0) {
 		if (service_registered(config, arglst[1])) {
 			asprintf(&sendstr, "Service already registered.\n");
+			status = 1;
 		} else {
 			svs = make_service(arglst[1]);
-			if (!svs)
+			if (!svs) {
 				asprintf(&sendstr, "Error building process structure.\n");
-			else if (!service_running(svs->svname) && start_service(svs))
+				status = 1;
+			} else if (!service_running(svs->svname) && start_service(svs)) {
 				asprintf(&sendstr, "Could not start service.\n");
-			else if (register_service(config, svs))
+				status = 1;
+			} else if (register_service(config, svs)) {
 				asprintf(&sendstr, "Could not monitor service.\n");
-			else
+				status = 1;
+			} else {
 				asprintf(&sendstr, "Monitoring service.\n");
+				status = 0;
+			}
 		}
+		send(sock_fd, &status, 1, 0);
+		send(sock_fd, sendstr, strlen(sendstr), 0);
+		send(sock_fd, &eot, 1, 0);
+		free(sendstr);
+		pthread_mutex_unlock(&config->service_mtx);
+		return 0;
+
 	/* disable */
 	} else if (strcmp(arglst[0], "disable") == 0) {
-		if (unregister_service(config, arglst[1]))
+		if (unregister_service(config, arglst[1])) {
 			asprintf(&sendstr, "Removing service failed: Not found.\n");
-		else
+			status = 1;
+		} else {
 			asprintf(&sendstr, "Service removed.\n");
+			status = 0;
+		}
+		send(sock_fd, &status, 1, 0);
+		send(sock_fd, sendstr, strlen(sendstr), 0);
+		send(sock_fd, &eot, 1, 0);
+		free(sendstr);
+		pthread_mutex_unlock(&config->service_mtx);
+		return 0;
+
 	/* shutdown */
 	} else if (strcmp(arglst[0], "shutdown") == 0) {
+		asprintf(&sendstr, "fscd shutting down.\n");
+		status = 0;
+		send(sock_fd, &status, 1, 0);
+		send(sock_fd, sendstr, strlen(sendstr), 0);
+		send(sock_fd, &eot, 1, 0);
+		free(sendstr);
 		pthread_mutex_unlock(&config->service_mtx); /* shutdown needs the lock. */
-		if (asprintf(&sendstr, "fscd shutting down.\n") <= 0) {
-			send(sock_fd, &eot, 1, 0);
-		} else {
-			send(sock_fd, sendstr, strlen(sendstr), 0);
-			send(sock_fd, &eot, 1, 0);
-		}
 		fscd_shutdown(config, 0);
+		return 0;
+
 	/* status */
 	} else if (strcmp(arglst[0], "status") == 0) {
 		print_status(config, sock_fd);
 		pthread_mutex_unlock(&config->service_mtx);
 		return 0;
-	} else {
-		pthread_mutex_unlock(&config->service_mtx);
-		return -1;
 	}
-	pthread_mutex_unlock(&config->service_mtx);
 
-	send(sock_fd, sendstr, strlen(sendstr), 0);
-	send(sock_fd, &eot, 4, 0);
-	if (sendstr)
-		free(sendstr);
-	return 0;
+	pthread_mutex_unlock(&config->service_mtx);
+	return -1;
 }
 
 /*
@@ -1094,8 +1138,7 @@
 	(void)unlink(socketname);
 
 #if defined(__FreeBSD__)
-	struct pidfh *pfh;
-	if (pidfile_remove(pfh))
+	if (pfh && pidfile_remove(pfh))
 		err(1, "pidfile_remove");
 #endif
 	exit(exitcode);
@@ -1103,7 +1146,7 @@
 
 /*
  * Handle a signal.
- * XXX: Currently, there's no signal handling except for shutting down on the 
+ * XXX: Currently, there's no signal handling except for shutting down on the
  * registered signals. There might be some in the future.
  */
 static void
