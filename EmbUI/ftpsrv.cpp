//#define FTP_DEBUG
#include <ftpsrv.h>
#include <FTPServer.h>
#include <basicui.h>

FTPServer *ftpsrv = nullptr;

void ftp_start(void){
  if (!ftpsrv) ftpsrv = new FTPServer(LittleFS);
  if (ftpsrv) ftpsrv->begin(embui.paramVariant(P_ftp_usr) | String(P_ftp), embui.paramVariant(P_ftp_pwd) | String(P_ftp));
}

void ftp_stop(void){
  if (ftpsrv) {
    ftpsrv->stop();
    delete ftpsrv;
    ftpsrv = nullptr;
  }
}
void ftp_loop(void){
  if (ftpsrv) ftpsrv->handleFTP();
}

bool ftp_status(){ return ftpsrv; };

namespace basicui {

void block_settings_ftp(Interface *interf, JsonObject *data){
    if (!interf) return;
    interf->json_frame_interface();

    // Headline
    interf->json_section_main(T_SET_FTP, T_DICT[lang][TD::D_FTPSRV]);

    interf->checkbox(P_ftp, ftp_status(), F("Enable FTP Server"));     // FTP On/off

    interf->text(P_ftp_usr, embui.paramVariant(P_ftp_usr) | String(P_ftp), "FTP login");
    interf->password(P_ftp_pwd, embui.paramVariant(P_ftp_pwd) | String(P_ftp), T_DICT[lang][TD::D_Password]);
    interf->button_submit(T_SET_FTP, T_DICT[lang][TD::D_SAVE], P_BLUE);

    // close and send frame
    interf->json_frame_flush(); // main
}

void set_settings_ftp(Interface *interf, JsonObject *data){
    if (!data) return;

    bool newstate = (*data)[P_ftp];

    embui.var_dropnulls(P_ftp, newstate);                           // ftp on/off

    // set ftp login
    if ((*data)[P_ftp_usr] == P_ftp)
      embui.var_remove(P_ftp_usr);   // do not save default login
    else
      embui.var_dropnulls(P_ftp_usr, (*data)[P_ftp_usr]);

    // set ftp passwd
    if ((*data)[P_ftp_pwd] == P_ftp)
      embui.var_remove(P_ftp_pwd);
    else
      embui.var_dropnulls(P_ftp_pwd, (*data)[P_ftp_pwd]);    // do not save default pwd

    ftp_stop();
    LOG(println, F("UI: Stopping FTP Server"));

    if ( newstate ){
      ftp_start();
      LOG(println, F("UI: Starting FTP Server"));
    }

    if (interf) basicui::section_settings_frame(interf, nullptr);          // go to "Options" page
}

} // namespace basicui