if {[catch {

# define run engine funtion
source [file join {C:/lscc/radiant/2026.1} scripts tcl flow run_engine.tcl]
# define global variables
global para
set para(gui_mode) "1"
set para(prj_dir) "C:/Users/bphung/Documents/GitHub/Laser_Tag_Code_PCB/FPGA/radiant_project/ir_link_test"
if {![file exists {C:/Users/bphung/Documents/GitHub/Laser_Tag_Code_PCB/FPGA/radiant_project/ir_link_test/impl_1}]} {
  file mkdir {C:/Users/bphung/Documents/GitHub/Laser_Tag_Code_PCB/FPGA/radiant_project/ir_link_test/impl_1}
}
cd {C:/Users/bphung/Documents/GitHub/Laser_Tag_Code_PCB/FPGA/radiant_project/ir_link_test/impl_1}
# synthesize IPs
# synthesize VMs
# propgate constraints
file delete -force -- ir_link_test_impl_1_cpe.ldc
::radiant::runengine::run_engine_newmsg cpe -syn lse -f "ir_link_test_impl_1.cprj" -a "iCE40UP"  -o ir_link_test_impl_1_cpe.ldc
# synthesize top design
file delete -force -- ir_link_test_impl_1.vm ir_link_test_impl_1.ldc
::radiant::runengine::run_engine_newmsg synthesis -f "C:/Users/bphung/Documents/GitHub/Laser_Tag_Code_PCB/FPGA/radiant_project/ir_link_test/impl_1/ir_link_test_impl_1_lattice.synproj" -logfile "ir_link_test_impl_1_lattice.srp"
::radiant::runengine::run_postsyn [list -a iCE40UP -p iCE40UP5K -t SG48 -sp High-Performance_1.2V -oc Industrial -top -w -o ir_link_test_impl_1_syn.udb ir_link_test_impl_1.vm] [list ir_link_test_impl_1.ldc]

} out]} {
   ::radiant::runengine::runtime_log $out
   exit 1
}
