--- fscadm/fscadm.c.orig	2013-06-03 14:03:41 UTC
+++ fscadm/fscadm.c
@@ -97,7 +97,7 @@
 		usage();
 
 	/* shutdown, status */
-	if ((strcmp(argv[0], "shutdown") == 0) 
+	if ((strcmp(argv[0], "shutdown") == 0)
 			|| (strcmp(argv[0], "status") == 0)) {
 		if (argc != 1)
 			usage();
@@ -137,7 +137,7 @@
 			"\n"
 			"options:\n"
 			"        -V   Print out version.\n"
-			"        -s S Use socket S instead of standard.\n " );
+			"        -s S Use socket S instead of standard.\n" );
 	exit(EX_USAGE);
 }
 
@@ -158,9 +158,10 @@
 int
 daemonconnect(char *task)
 {
-	int s, len, nbytes, retcode = 0;
+	int s, len, nbytes, retcode = 1, total = 0;
 	struct sockaddr_un remote;
 	char recdata[LINE_MAX];
+	char *ptr;
 
 	if ((s = socket(PF_LOCAL, SOCK_STREAM, 0)) == -1)
 		err(EX_OSERR, "socket");
@@ -176,16 +177,22 @@
 		err(EX_OSERR, "send");
 
 	do {
-		memset(recdata, 0, sizeof(recdata));
 		nbytes = recv(s, recdata, sizeof(recdata) - 1, 0);
-		if (nbytes > 0)
-			printf("%s", recdata);
-	} while (recdata[strlen(recdata) - 1] != 4); /* 4 = EOT */
-
-	if (nbytes < 0) {
-		warn("recv");
-		retcode = nbytes;
-	}
+		if (nbytes <= 0) {
+			if (nbytes < 0) {
+				warn("recv");
+			}
+			break;
+		}
+		ptr = recdata;
+		if (!total) {
+			retcode = recdata[0];
+			ptr++;
+		}
+		total += nbytes;
+		recdata[nbytes] = '\0';
+		printf("%s", ptr);
+	} while (recdata[nbytes - 1] != 4); /* 4 = EOT */
 
 	close(s);
 	return retcode;
