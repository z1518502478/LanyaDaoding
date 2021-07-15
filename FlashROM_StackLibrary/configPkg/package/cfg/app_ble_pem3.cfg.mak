# invoke SourceDir generated makefile for app_ble.pem3
app_ble.pem3: .libraries,app_ble.pem3
.libraries,app_ble.pem3: package/cfg/app_ble_pem3.xdl
	$(MAKE) -f D:\Ti\cc2640r2l\multi_role\multi_role_cc2640r2lp_app\TOOLS/src/makefile.libs

clean::
	$(MAKE) -f D:\Ti\cc2640r2l\multi_role\multi_role_cc2640r2lp_app\TOOLS/src/makefile.libs clean

