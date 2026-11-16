if {[catch {

# define run engine funtion
source [file join {C:/lscc/radiant/2025.2.1} scripts tcl flow run_engine.tcl]
# define global variables
global para
set para(gui_mode) "1"
set para(prj_dir) "C:/Users/jgsen/Downloads/Laser Tag Code & PCB/Laser_Tag_Code_PCB/FPGA2/radiant_project/ir_decoder"
if {![file exists {C:/Users/jgsen/Downloads/Laser Tag Code & PCB/Laser_Tag_Code_PCB/FPGA2/radiant_project/ir_decoder/impl_1}]} {
  file mkdir {C:/Users/jgsen/Downloads/Laser Tag Code & PCB/Laser_Tag_Code_PCB/FPGA2/radiant_project/ir_decoder/impl_1}
}
cd {C:/Users/jgsen/Downloads/Laser Tag Code & PCB/Laser_Tag_Code_PCB/FPGA2/radiant_project/ir_decoder/impl_1}
# synthesize IPs
# synthesize VMs
# synthesize top design
file delete -force -- ir_decoder_impl_1.vm ir_decoder_impl_1.ldc
::radiant::runengine::run_engine_newmsg synthesis -f "C:/Users/jgsen/Downloads/Laser Tag Code & PCB/Laser_Tag_Code_PCB/FPGA2/radiant_project/ir_decoder/impl_1/ir_decoder_impl_1_lattice.synproj" -logfile "ir_decoder_impl_1_lattice.srp"
::radiant::runengine::run_postsyn [list -a iCE40UP -p iCE40UP5K -t SG48 -sp High-Performance_1.2V -oc Industrial -top -w -o ir_decoder_impl_1_syn.udb ir_decoder_impl_1.vm] [list ir_decoder_impl_1.ldc]

} out]} {
   ::radiant::runengine::runtime_log $out
   exit 1
}
