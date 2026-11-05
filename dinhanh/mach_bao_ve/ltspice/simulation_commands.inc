* Simulation setup for input_protection_1ch.asc
.include protection_models.lib
.param VSUP=3.3 TSTOP=25.00004m TMAX=1u
.options plotwinsize=0 method=gear maxord=1
.meas tran A_MAX MAX V(A)
.meas tran CLAMP_MAX MAX V(CLAMP)
.meas tran OUT_MAX MAX V(OUT)
.meas tran IIN_MIN MIN I(VIN)

* Timing at the first 5V falling edge, measured at VIN 50% and OUT 50%.
.meas tran IN_F WHEN V(IN)=2.5 FALL=1 TD=13.5m
.meas tran A_F WHEN V(A)=1.1 FALL=1 TD=13.5m
.meas tran OUT_F WHEN V(OUT)=1.65 FALL=1 TD=13.5m
.meas tran DELAY_F PARAM OUT_F-IN_F
.meas tran BUFFER_F PARAM OUT_F-A_F
