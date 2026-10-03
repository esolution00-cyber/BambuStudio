#include <regex>
#include "CalibrationWizardPresetPage.hpp"
#include "I18N.hpp"
#include "Widgets/Label.hpp"
#include "MsgDialog.hpp"
#include "libslic3r/Print.hpp"
#include "BBLUtil.hpp"

#include "DeviceCore/DevConfig.h"
#include "DeviceCore/DevExtruderSystem.h"
#include "DeviceCore/DevFilaBlackList.h"
#include "DeviceCore/DevFilaSystem.h"
#include "DeviceCore/DevStorage.h"
#include "DeviceCore/DevNozzleSystem.h"
#include "DeviceCore/DevNozzleRack.h"
#include "DeviceCore/DevConfigUtil.h"
#include "DeviceCore/DevUpgrade.h"

#include "DeviceCore/DevManager.h"
#include "DeviceCore/DevFilaSystem.h"

#define CALIBRATION_LABEL_SIZE wxSize(FromDIP(150), FromDIP(24))
#define SYNC_BUTTON_SIZE (wxSize(FromDIP(50), FromDIP(50)))
#define CALIBRATION_TEXT_INPUT_Y_SIZE FromDIP(20)

#define LEFT_EXTRUDER_ID  1
#define RIGHT_EXTRUDER_ID 0

#define MAX_SLOT_NUM 4

namespace Slic3r { namespace GUI {
static int PA_LINE = 0;
static int PA_PATTERN = 1;

static std::vector<NozzleVolumeType> volumes = { nvtStandard, nvtHighFlow, nvtTPUHighFlow, nvtHybrid};

static DynamicPrintConfig get_empty_dynamic_print_config() {
    DynamicPrintConfig empty_config;
    empty_config.set_key_value("filament_id", new ConfigOptionStrings{""});
    empty_config.set_key_value("tag_uid", new ConfigOptionStrings{""});
    empty_config.set_key_value("filament_type", new ConfigOptionStrings{""});
    empty_config.set_key_value("tray_name", new ConfigOptionStrings{""});
    empty_config.set_key_value("filament_colour", new ConfigOptionStrings{""});
    empty_config.set_key_value("filament_exist", new ConfigOptionBools{false});

    return empty_config;
}

CaliPresetCaliStagePanel::CaliPresetCaliStagePanel(
    wxWindow* parent,
    wxWindowID id,
    const wxPoint& pos,
    const wxSize& size,
    long style)
    : wxPanel(parent, id, pos, size, style)
{
    SetBackgroundColour(*wxWHITE);

    m_top_sizer = new wxBoxSizer(wxVERTICAL);

    create_panel(this);

    this->SetSizer(m_top_sizer);
    m_top_sizer->Fit(this);
}

void CaliPresetCaliStagePanel::msw_rescale()
{
    flow_ratio_input->GetTextCtrl()->SetSize(wxSize(-1, CALIBRATION_TEXT_INPUT_Y_SIZE));
}

void CaliPresetCaliStagePanel::create_panel(wxWindow* parent)
{
    auto title = new Label(parent, _L("Calibration Type"));
    title->SetFont(Label::Head_14);
    m_top_sizer->Add(title);
    m_top_sizer->AddSpacer(FromDIP(15));

    m_complete_radioBox = new wxRadioButton(parent, wxID_ANY, _L("Complete Calibration"));
    m_complete_radioBox->SetForegroundColour(*wxBLACK);

    m_complete_radioBox->SetValue(true);
    m_stage = CALI_MANUAL_STAGE_1;
    m_top_sizer->Add(m_complete_radioBox);
    m_top_sizer->AddSpacer(FromDIP(10));
    m_fine_radioBox = new wxRadioButton(parent, wxID_ANY, _L("Fine Calibration based on flow ratio"));
    m_fine_radioBox->SetForegroundColour(*wxBLACK);
    m_top_sizer->Add(m_fine_radioBox);

    input_panel = new wxPanel(parent);
    input_panel->Hide();
    auto input_sizer = new wxBoxSizer(wxHORIZONTAL);
    input_panel->SetSizer(input_sizer);
    flow_ratio_input = new TextInput(input_panel, wxEmptyString, "", "", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE);
    flow_ratio_input->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    float default_flow_ratio = 1.0f;
    auto flow_ratio_str = wxString::Format("%.2f", default_flow_ratio);
    flow_ratio_input->GetTextCtrl()->SetValue(flow_ratio_str);
    input_sizer->AddSpacer(FromDIP(18));
    input_sizer->Add(flow_ratio_input, 0, wxTOP, FromDIP(10));
    m_top_sizer->Add(input_panel);

    m_top_sizer->AddSpacer(PRESET_GAP);
    // events
    m_complete_radioBox->Bind(wxEVT_RADIOBUTTON, [this](auto& e) {
        m_stage_panel_parent->get_current_object()->GetCalib()->SetFlowRatioCalibType(COMPLETE_CALIBRATION);
        input_panel->Show(false);
        m_stage = CALI_MANUAL_STAGE_1;
        GetParent()->Layout();
        GetParent()->Fit();
        });
    m_fine_radioBox->Bind(wxEVT_RADIOBUTTON, [this](auto& e) {
        m_stage_panel_parent->get_current_object()->GetCalib()->SetFlowRatioCalibType(FINE_CALIBRATION);
        input_panel->Show();
        m_stage = CALI_MANUAL_STAGE_2;
        GetParent()->Layout();
        GetParent()->Fit();
        });
    flow_ratio_input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this](auto& e) {
        float flow_ratio = 0.0f;
        if (!CalibUtils::validate_input_flow_ratio(flow_ratio_input->GetTextCtrl()->GetValue(), &flow_ratio)) {
            MessageDialog msg_dlg(nullptr, _L("Please input a valid value (0.0 < flow ratio < 2.0)"), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
        }
        auto flow_ratio_str = wxString::Format("%.3f", flow_ratio);
        flow_ratio_input->GetTextCtrl()->SetValue(flow_ratio_str);
        m_flow_ratio_value = flow_ratio;
        });
    flow_ratio_input->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this](auto& e) {
        float flow_ratio = 0.0f;
        if (!CalibUtils::validate_input_flow_ratio(flow_ratio_input->GetTextCtrl()->GetValue(), &flow_ratio)) {
            MessageDialog msg_dlg(nullptr, _L("Please input a valid value (0.0 < flow ratio < 2.0)"), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
        }
        auto flow_ratio_str = wxString::Format("%.3f", flow_ratio);
        flow_ratio_input->GetTextCtrl()->SetValue(flow_ratio_str);
        m_flow_ratio_value = flow_ratio;
        e.Skip();
        });
    Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        SetFocusIgnoringChildren();
        });
}

void CaliPresetCaliStagePanel::set_cali_stage(CaliPresetStage stage, float value)
{
    if (stage == CaliPresetStage::CALI_MANUAL_STAGE_1) {
        wxCommandEvent radioBox_evt(wxEVT_RADIOBUTTON);
        radioBox_evt.SetEventObject(m_complete_radioBox);
        wxPostEvent(m_complete_radioBox, radioBox_evt);
        m_stage = stage;
    }
    else if(stage == CaliPresetStage::CALI_MANUAL_STAGE_2){
        wxCommandEvent radioBox_evt(wxEVT_RADIOBUTTON);
        radioBox_evt.SetEventObject(m_fine_radioBox);
        wxPostEvent(m_fine_radioBox, radioBox_evt);
        m_stage = stage;
        m_flow_ratio_value = value;
    }
}

void CaliPresetCaliStagePanel::get_cali_stage(CaliPresetStage& stage, float& value)
{
    stage = m_stage;
    if (m_stage != CALI_MANUAL_STAGE_2)
        return;

    float flow_ratio = 0.0f;
    if (!CalibUtils::validate_input_flow_ratio(flow_ratio_input->GetTextCtrl()->GetValue(), &flow_ratio)) {
        MessageDialog msg_dlg(nullptr, _L("Please input a valid value (0.0 < flow ratio < 2.0)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        value = NAN;
        return;
    }

    m_flow_ratio_value = flow_ratio;
    value = flow_ratio;
}
    std::string incompatiable_filament_name;
    std::string error_tips;
    int bed_temp = 0;
    return is_filaments_compatiable(prests, bed_temp, incompatiable_filament_name, error_tips);
}

bool CalibrationPresetPage::is_filament_in_blacklist(int tray_id, Preset* preset, std::string& error_tips)
{
    if (!curr_obj)
        return true;

    int ams_id;
    int slot_id;
    int out_tray_id;
    get_tray_ams_and_slot_id(curr_obj, tray_id, ams_id, slot_id, out_tray_id);

    if (wxGetApp().app_config->get("skip_ams_blacklist_check") != "true") {
        DevFilaBlacklist::CheckResult result;
        DevFilaBlacklist::CheckFilamentInfo check_info;
        check_info.dev_id = curr_obj->get_dev_id();
        check_info.model_id = curr_obj->printer_type;
        check_info.fila_id = preset->filament_id;
        preset->get_filament_type(check_info.fila_type);
        check_info.ams_id = ams_id;
        check_info.slot_id = slot_id;
        check_info.nozzle_flow = curr_obj->GetFilaSystem()->GetNozzleFlowStringByAmsId(std::to_string(ams_id)); // NOTE: to be fixed
        check_info.fila_name = preset->alias;
        check_info.has_filament_switch = curr_obj->GetFilaSwitch()->IsInstalled();

        if (!curr_obj->GetNozzleRack()->IsSupported()) {
            int extruder_id = curr_obj->GetFilaSystem()->GetExtruderIdByAmsId(std::to_string(ams_id));
            check_info.nozzle_diameter = curr_obj->GetExtderSystem()->GetNozzleDiameter(extruder_id);
        }

        if (m_cali_method == CalibrationMethod::CALI_METHOD_AUTO || m_cali_method == CalibrationMethod::CALI_METHOD_NEW_AUTO)
            check_info.calib_mode = "auto_pa";

        auto vendor = dynamic_cast<ConfigOptionStrings*> (preset->config.option("filament_vendor"));
        if (vendor && (vendor->values.size() > 0)) {
            check_info.fila_vendor = vendor->values[0];
        }
        result = DevFilaBlacklist::check_filaments_in_blacklist(check_info);

        if (const auto& prohibition_items = result.get_items_by_action("prohibition"); !prohibition_items.empty()) {
            wxString combined_msg;
            for (auto item : prohibition_items) {
                combined_msg += item.info_msg + "\n";
            }

            error_tips = combined_msg.ToUTF8().data();
            return false;
        }

        if (const auto& warning_items = result.get_items_by_action("warning"); !warning_items.empty()) {
            wxString combined_msg;
            for (auto item : warning_items) {
                combined_msg += item.info_msg + "\n";
            }

            error_tips = combined_msg.ToUTF8().data();
            return true;
        }

        return true;
    }
    if (devPrinterUtil::IsVirtualSlot(ams_id)) {
        if (m_cali_mode == CalibMode::Calib_PA_Line && (m_cali_method == CalibrationMethod::CALI_METHOD_AUTO || m_cali_method == CalibrationMethod::CALI_METHOD_NEW_AUTO)) {
            std::string filamnt_type;
            preset->get_filament_type(filamnt_type);
            if (filamnt_type == "TPU") {
                error_tips = _u8L("TPU is not supported for Flow Dynamics Auto-Calibration.");
                return false;
            }
        }
    }
    return true;
}

bool CalibrationPresetPage::is_filaments_compatiable(const std::vector<CaliFilamentInfo> &prests,
    int& bed_temp,
    std::string& incompatiable_filament_name,
    std::string& error_tips)
{
    if (prests.empty()) return true;

    bed_temp = 0;
    std::vector<std::string> filament_types;
    for (auto &item : prests) {
        const auto& item_preset = item.filament_preset;
        if (!item_preset)
            continue;

        // update bed temperature
        BedType curr_bed_type = BedType(m_displayed_bed_types[m_comboBox_bed_type->GetSelection()]);
        const ConfigOptionInts *opt_bed_temp_ints = item_preset->config.option<ConfigOptionInts>(get_bed_temp_key(curr_bed_type));
        int bed_temp_int = 0;
        if (opt_bed_temp_ints) {
            bed_temp_int = opt_bed_temp_ints->get_at(0);
        }

        if (bed_temp_int <= 0) {
            if (!item_preset->alias.empty())
                incompatiable_filament_name = item_preset->alias;
            else
                incompatiable_filament_name = item_preset->name;

            return false;
        } else {
            // set for first preset
            if (bed_temp == 0)
                bed_temp = bed_temp_int;
        }
        std::string display_filament_type;
        filament_types.push_back(item_preset->config.get_filament_type(display_filament_type, 0));

        // check is it in the filament blacklist
        if (!is_filament_in_blacklist(item.tray_id, item_preset, error_tips))
            return false;
    }

    if (Print::check_multi_filaments_compatibility(filament_types) == FilamentCompatibilityType::HighLowMixed) {
        error_tips = _u8L("Can not print multiple filaments which have large difference of temperature together. Otherwise, the extruder and nozzle may be blocked or damaged during printing");
        return false;
    }

    return true;
}

void CalibrationPresetPage::check_filament_cali_reliability(const std::vector<Preset *> &prests)
{
    m_warning_panel->Hide();
    if (!curr_obj)
        return;

    if (m_cali_method == CALI_METHOD_AUTO) {
        std::set<std::string> foam_filaments;
        for (auto &item : prests) {
            if (!item)
                continue;

            if (item->filament_id == "GFA11" || item->filament_id == "GFB02") {  // PLA Aero, ASA-Aero
                if (item->alias.empty())
                    foam_filaments.insert(item->name);
                else
                    foam_filaments.insert(item->alias);
            }
        }

        if (!foam_filaments.empty()) {
            std::string names;
            for (auto foam_filament : foam_filaments) {
                names += foam_filament;
                names += ",";
            }
            names.pop_back();

            wxString tips;
            if (curr_obj->get_printer_series() == PrinterSeries::SERIES_X1) {
                tips = (boost::format(_u8L("Tip: Calibrating foam filaments(%s) in X series printers may not get accurate results\n"
                                          "because their dynamic response is much different from that of ordinary filaments, and there is a high risk of oozing when printing calibration lines.")) %names).str();
            }
            else if (curr_obj->get_printer_arch() == PrinterArch::ARCH_I3) {
                tips = (boost::format(_u8L("Tip: When using the A1/A1 mini printer, we do not recommend calibrating foam filaments(%s),\n"
                                           "as the results may be unstable and affect print quality."))%names).str();
            }
            m_warning_panel->set_warning(tips);
            m_warning_panel->Show();
        }
    }
}

void CalibrationPresetPage::update_plate_type_collection(CalibrationMethod method)
{
    m_comboBox_bed_type->Clear();
    const ConfigOptionDef* bed_type_def = print_config_def.get("curr_bed_type");
    if (bed_type_def && bed_type_def->enum_keys_map) {
        for (int i = 0; i < bed_type_def->enum_labels.size(); i++) {
            m_comboBox_bed_type->AppendString(_L(bed_type_def->enum_labels[i]));
        }
        m_comboBox_bed_type->SetSelection(0);
    }
}

void CalibrationPresetPage::update_combobox_filaments(MachineObject* obj)
{
    if (!obj) return;

    if (!obj->is_info_ready())
        return;

    //step 1: update combobox filament list
    float nozzle_value = get_nozzle_value();
    obj->GetCalib()->SetSelectedNozzleDiameter(DevNozzle::ToNozzleDiameterType(nozzle_value));
    if (nozzle_value < 1e-3) {
        return;
    }

    Preset* printer_preset = get_printer_preset(obj, nozzle_value);
    if (!printer_preset)
        return;

    auto opt_extruder_type = printer_preset->config.option<ConfigOptionEnumsGeneric>("extruder_type");
    if (opt_extruder_type) {
        assert(opt_extruder_type->values.size() <= 2);
        for (size_t i = 0; i < opt_extruder_type->values.size(); ++i) {
            m_extrder_types[i] = (ExtruderType)(opt_extruder_type->values[i]);
        }
    }

    // sync ams filaments list info
    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle && printer_preset) {
        preset_bundle->set_calibrate_printer(printer_preset->name);
    }

    //step 2: sync ams info from object by default
    sync_ams_info(obj);

    //step 3: select the default compatible filament to calibration
    select_default_compatible_filament();
}

bool CalibrationPresetPage::is_blocking_printing()
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return true;

    MachineObject* obj_ = dev->get_selected_machine();
    if (obj_ == nullptr) return true;

    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    auto source_model = preset_bundle->printers.get_edited_preset().get_printer_type(preset_bundle);
    auto target_model = obj_->printer_type;

    if (source_model != target_model) {
        std::vector<std::string> compatible_machine = obj_->get_compatible_machine();
        vector<std::string>::iterator it = find(compatible_machine.begin(), compatible_machine.end(), source_model);
        if (it == compatible_machine.end()) {
            return true;
        }
    }

    return false;
}

void CalibrationPresetPage::update_sync_button_status()
{
    auto set_status = [this](bool synced) {
        StateColor synced_colour(std::pair<wxColour, int>(wxColour("#CECECE"), StateColor::Normal));
        StateColor not_synced_colour(std::pair<wxColour, int>(wxColour("#00AE42"), StateColor::Normal));
        if (synced) {
            m_btn_sync->SetBorderColor(synced_colour);
            m_btn_sync->SetIcon("ams_nozzle_sync");
            m_sync_button_text->SetLabel(_L("AMS and nozzle information are synced"));
        } else {
            m_btn_sync->SetBorderColor(not_synced_colour);
            m_btn_sync->SetIcon("printer_sync");
            m_sync_button_text->SetLabel(_L("Sync AMS and nozzle information"));
        }
    };

    if (!curr_obj || !curr_obj->is_info_ready()) {
        set_status(false);
        return;
    }

    struct CaliNozzleInfo
    {
        float nozzle_diameter{0.4f};
        int   nozzle_volume_type{0};

        bool operator==(const CaliNozzleInfo &other) const
        {
            return abs(nozzle_diameter - other.nozzle_diameter) < EPSILON
                && nozzle_volume_type == other.nozzle_volume_type;
        }
    };

    if (curr_obj->is_multi_extruders()) {
        std::vector<CaliNozzleInfo> machine_obj_nozzle_infos;
        machine_obj_nozzle_infos.resize(2);
        for (const DevExtder& extruder : curr_obj->GetExtderSystem()->GetExtruders()) {
            machine_obj_nozzle_infos[extruder.GetExtId()].nozzle_diameter = extruder.GetNozzleDiameter();
            machine_obj_nozzle_infos[extruder.GetExtId()].nozzle_volume_type = int(extruder.GetNozzleFlowType()) - 1;
        }

        std::vector<CaliNozzleInfo> cali_nozzle_infos;
        cali_nozzle_infos.resize(2);
        for (size_t extruder_id = 0; extruder_id < 2; ++extruder_id) {
            cali_nozzle_infos[extruder_id].nozzle_diameter = get_nozzle_diameter(extruder_id);
            cali_nozzle_infos[extruder_id].nozzle_volume_type = int(get_nozzle_volume_type(extruder_id));
        }

        set_status(machine_obj_nozzle_infos == cali_nozzle_infos);
    }
    else {
        bool is_equal = is_approx(curr_obj->GetExtderSystem()->GetNozzleDiameter(0), get_nozzle_diameter(0));
        set_status(is_equal);
    }
}

void CalibrationPresetPage::update_show_status()
{
    NetworkAgent* agent = Slic3r::GUI::wxGetApp().getAgent();
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!agent) {return;}
    if (!dev) return;

    MachineObject* obj_ = dev->get_selected_machine();
    if (!obj_) {
        if (agent->is_user_login()) {
            show_status(CaliPresetPageStatus::CaliPresetStatusInvalidPrinter);
        }
        else {
            show_status(CaliPresetPageStatus::CaliPresetStatusNoUserLogin);
        }
        return;
    }

    if (!obj_->is_lan_mode_printer()) {
        if (!agent->is_server_connected()) {
            show_status(CaliPresetPageStatus::CaliPresetStatusConnectingServer);
            return;
        }
    }

    auto upgrade_ptr = obj_->GetUpgrade().lock();
    if (upgrade_ptr) {
        if (upgrade_ptr->IsUpgradeForceUpgrade()) {
            show_status(CaliPresetStatusNeedForceUpgrading);
            return;
        }

        if (upgrade_ptr->IsUpgradeConsistencyRequest()) {
            show_status(CaliPresetStatusNeedConsistencyUpgrading);
            return;
        }
    }

    //if (is_blocking_printing()) {
    //    show_status(CaliPresetPageStatus::CaliPresetStatusUnsupportedPrinter);
    //    return;
    //}
    //else
    if (obj_->is_connecting() || !obj_->is_connected()) {
        show_status(CaliPresetPageStatus::CaliPresetStatusInConnecting);
        return;
    }
    else if (obj_->is_in_upgrading()) {
        show_status(CaliPresetPageStatus::CaliPresetStatusInUpgrading);
        return;
    }
    else if (obj_->is_system_printing()) {
        show_status(CaliPresetPageStatus::CaliPresetStatusInSystemPrinting);
        return;
    }
    else if (obj_->is_in_printing()
          || obj_->ams_status_main == AMS_STATUS_MAIN_FILAMENT_CHANGE
          || obj_->ams_status_main == AMS_STATUS_MAIN_COLD_PULL) {
        show_status(CaliPresetPageStatus::CaliPresetStatusInPrinting);
        return;
    }

    //if (obj_->is_multi_extruders()) {
    //    float diameter = obj_->m_extder_data.extders[0].current_nozzle_diameter;
    //    bool  is_same_diameter = std::all_of(obj_->m_extder_data.extders.begin(), obj_->m_extder_data.extders.end(),
    //       [diameter](const Extder& extruder) {
    //            return std::fabs(extruder.current_nozzle_diameter - diameter) < EPSILON;
    //       });
    //    if (!is_same_diameter) {
    //        show_status(CaliPresetPageStatus::CaliPresetStatusDifferentNozzleDiameters);
    //        return;
    //    }
    //}

    // check sdcard when if lan mode printer
    if (obj_->is_lan_mode_printer()) {
        if (obj_->GetStorage()->get_sdcard_state() == DevStorage::SdcardState::NO_SDCARD
            && !obj_->is_support_print_with_emmc) {
            show_status(CaliPresetPageStatus::CaliPresetStatusLanModeNoSdcard);
            return;
        } else if (obj_->GetStorage()->get_sdcard_state() == DevStorage::SdcardState::HAS_SDCARD_ABNORMAL ||
                   obj_->GetStorage()->get_sdcard_state() == DevStorage::SdcardState::HAS_SDCARD_READONLY) {
            show_status(CaliPresetPageStatus::CaliPresetStatusLanModeSDcardNotAvailable);
            return;
        }
    }
    else if (!obj_->GetConfig()->SupportPrintWithoutSD() && (obj_->GetStorage()->get_sdcard_state() == DevStorage::SdcardState::NO_SDCARD))
    {
        show_status(CaliPresetPageStatus::CaliPresetStatusNoSdcard);
        return;
    }

    if (m_has_filament_incompatible) {
        show_status(CaliPresetPageStatus::CaliPresetStatusFilamentIncompatible);
        return;
    }

    show_status(CaliPresetPageStatus::CaliPresetStatusNormal);
}


bool CalibrationPresetPage::need_check_sdcard(MachineObject* obj)
{
    if (!obj) return false;

    bool need_check = false;
    if (obj->get_printer_series() == PrinterSeries::SERIES_X1) {
        if (m_cali_mode == CalibMode::Calib_Flow_Rate && m_cali_method == CalibrationMethod::CALI_METHOD_MANUAL) {
            need_check = true;
        }
        else if (m_cali_mode == CalibMode::Calib_Vol_speed_Tower && m_cali_method == CalibrationMethod::CALI_METHOD_MANUAL)
        {
            need_check =  true;
        }
    }
    else if (obj->get_printer_series() == PrinterSeries::SERIES_P1P) {
        if (m_cali_mode == CalibMode::Calib_Flow_Rate && m_cali_method == CalibrationMethod::CALI_METHOD_MANUAL) {
            need_check =  true;
        }
        else if (m_cali_mode == CalibMode::Calib_Vol_speed_Tower && m_cali_method == CalibrationMethod::CALI_METHOD_MANUAL) {
            need_check =  true;
        }
    }
    else {
        assert(false);
        return false;
    }

    return need_check;
}

void CalibrationPresetPage::show_status(CaliPresetPageStatus status)
{
    if (m_stop_update_page_status)
        return;

    if (m_page_status != status)
        m_page_status = status;
    else
        return;

    // other
    if (status == CaliPresetPageStatus::CaliPresetStatusInit) {
        update_print_status_msg(wxEmptyString, false);
        Enable_Send_Button(false);
    }
    else if (status == CaliPresetPageStatus::CaliPresetStatusNormal) {
        update_print_status_msg(wxEmptyString, false);
        Enable_Send_Button(true);
        Layout();
        Fit();
    }
    else if (status == CaliPresetPageStatus::CaliPresetStatusNoUserLogin) {
        wxString msg_text = _L("No login account, only printers in LAN mode are displayed");
        update_print_status_msg(msg_text, false);
        Enable_Send_Button(false);
    }
    else if (status == CaliPresetPageStatus::CaliPresetStatusInvalidPrinter) {
        update_print_status_msg(wxEmptyString, true);
        Enable_Send_Button(false);
    }
    else if (status == CaliPresetPageStatus::CaliPresetStatusConnectingServer) {
        wxString msg_text = _L("Connecting to server");
        update_print_status_msg(msg_text, true);
        Enable_Send_Button(false);
    }
    else if (status == CaliPresetPageStatus::CaliPresetStatusInUpgrading) {
        wxString msg_text = _L("Cannot send the print job when the printer is updating firmware");
        update_print_status_msg(msg_text, true);
        Enable_Send_Button(false);
    }
    else if (status == CaliPresetPageStatus::CaliPresetStatusInSystemPrinting) {
        wxString msg_text = _L("The printer is executing instructions. Please restart printing after it ends");
        update_print_status_msg(msg_text, true);
        Enable_Send_Button(false);
    }
    else if (status == CaliPresetPageStatus::CaliPresetStatusInPrinting) {
        wxString msg_text = _L("The printer is busy on other print job");
        update_print_status_msg(msg_text, true);
        Enable_Send_Button(false);
    }
    else if (status == CaliPresetPageStatus::CaliPresetStatusLanModeNoSdcard) {
        wxString msg_text = _L("Storage needs to be inserted before printing via LAN.");
        update_print_status_msg(msg_text, true);
        Enable_Send_Button(false);
    }
    else if (status == CaliPresetPageStatus::CaliPresetStatusLanModeSDcardNotAvailable) {
        wxString msg_text = _L("Storage is not available or is in read-only mode.");
        update_print_status_msg(msg_text, true);
        Enable_Send_Button(false);
    }
    else if (status == CaliPresetPageStatus::CaliPresetStatusNoSdcard) {
        wxString msg_text = _L("Storage needs to be inserted before printing.");
        update_print_status_msg(msg_text, true);
        Enable_Send_Button(false);
    }
    else if (status == CaliPresetPageStatus::CaliPresetStatusNeedForceUpgrading) {
        wxString msg_text = _L("Cannot send the print job to a printer whose firmware is required to get updated.");
        update_print_status_msg(msg_text, true);
        Enable_Send_Button(false);
    }
    else if (status == CaliPresetPageStatus::CaliPresetStatusNeedConsistencyUpgrading) {
        wxString msg_text = _L("Cannot send the print job to a printer whose firmware is required to get updated.");
        update_print_status_msg(msg_text, true);
        Enable_Send_Button(false);
    }
    else if (status == CaliPresetPageStatus::CaliPresetStatusFilamentIncompatible) {
        update_print_status_msg(wxEmptyString, false);
        Enable_Send_Button(false);
    }
    else if (status == CaliPresetPageStatus::CaliPresetStatusInConnecting) {
        wxString msg_text = _L("Connecting to printer");
        update_print_status_msg(msg_text, true);
        Enable_Send_Button(false);
    }
    else if (status == CaliPresetPageStatus::CaliPresetStatusDifferentNozzleDiameters) {
        std::string cwp_msg_pt = curr_obj ? curr_obj->printer_type : wxGetApp().preset_bundle->printers.get_edited_preset().get_printer_type(wxGetApp().preset_bundle);
        wxString msg_text = wxString::Format(_L("Calibration only supports cases where the %s and %s diameters are identical."),
            _L(DevPrinterConfigUtil::get_toolhead_display_name(cwp_msg_pt, MAIN_EXTRUDER_ID, ToolHeadComponent::Nozzle, ToolHeadNameCase::LowerCase)),
            _L(DevPrinterConfigUtil::get_toolhead_display_name(cwp_msg_pt, DEPUTY_EXTRUDER_ID, ToolHeadComponent::Nozzle, ToolHeadNameCase::LowerCase)));
        update_print_status_msg(msg_text, true);
        Enable_Send_Button(false);
    }

    Layout();
}

void CalibrationPresetPage::Enable_Send_Button(bool enable)
{
    m_action_panel->enable_button(CaliPageActionType::CALI_ACTION_CALI, enable);
}

float CalibrationPresetPage::get_nozzle_value()
{
    double nozzle_value = 0.0;

    auto sel = m_comboBox_nozzle_dia->GetSelection();
    if (sel != wxNOT_FOUND) {
        auto diameter = NozzleDiameterType(*(int*)m_comboBox_nozzle_dia->GetClientData(sel));
        nozzle_value = DevNozzle::ToNozzleDiameterFloat(diameter);
    }

    return nozzle_value;
}

void CalibrationPresetPage::update(MachineObject* obj)
{
    curr_obj = obj;

    //update printer status
    update_show_status();

    update_sync_button_status();
}

void CalibrationPresetPage::on_device_connected(MachineObject* obj)
{
    init_selection_values();
    init_with_machine(obj);
    update_combobox_filaments(obj);
}

void CalibrationPresetPage::set_cali_filament_mode(CalibrationFilamentMode mode)
{
    CalibrationWizardPage::set_cali_filament_mode(mode);

    for (int i = 0; i < m_filament_comboBox_list.size(); i++) {
        m_filament_comboBox_list[i]->set_select_mode(mode);
    }

    if (mode == CALI_MODEL_MULITI) {
        m_filament_list_tips->Show();
    } else {
        m_filament_list_tips->Hide();
    }
}

void CalibrationPresetPage::set_cali_method(CalibrationMethod method)
{
    CalibrationWizardPage::set_cali_method(method);
    if (method == CalibrationMethod::CALI_METHOD_MANUAL) {
        if (m_cali_mode == CalibMode::Calib_Flow_Rate) {
            wxArrayString steps;
            steps.Add(_L("Preset"));
            steps.Add(_L("Calibration1"));
            steps.Add(_L("Calibration2"));
            steps.Add(_L("Record Factor"));
            m_step_panel->set_steps_string(steps);
            m_step_panel->set_steps(0);
            if (m_cali_stage_panel)
                m_cali_stage_panel->Show();

            if (m_pa_cali_method_combox)
                m_pa_cali_method_combox->Show(false);

            if (m_custom_range_panel)
                m_custom_range_panel->Show(false);
        }
        else if (m_cali_mode == CalibMode::Calib_PA_Line || m_cali_mode == CalibMode::Calib_PA_Pattern) {
            if (m_cali_stage_panel)
                m_cali_stage_panel->Show(false);

            if (m_pa_cali_method_combox)
                m_pa_cali_method_combox->Show();

            if (m_custom_range_panel) {
                wxArrayString titles;
                titles.push_back(_L("From k Value"));
                titles.push_back(_L("To k Value"));
                titles.push_back(_L("Value step"));
                m_custom_range_panel->set_titles(titles);

                wxArrayString values;
                ExtruderType extruder_type  = ExtruderType::etDirectDrive;
                Preset* printer_preset = get_printer_preset(curr_obj, get_nozzle_value());
                std::vector<FilamentComboBox *> selected_filament = get_selected_filament_combobox();
                if (!selected_filament.empty() && printer_preset) {
                    int tray_id     = selected_filament[0]->get_tray_id();
                    int out_tray_id = tray_id;
                    int ams_id      = 0;
                    int slot_id     = 0;
                    get_tray_ams_and_slot_id(curr_obj, tray_id, ams_id, slot_id, out_tray_id);
                    int              extruder_id            = selected_filament[0]->GetExtuderRole() == ExtruderRole::DEPUTY_EXTRUDER ? DEPUTY_EXTRUDER_ID : MAIN_EXTRUDER_ID;
                    std::vector<int> physical_extruder_maps = dynamic_cast<ConfigOptionInts *>(printer_preset->config.option("physical_extruder_map"))->values;
                    int              extruder_count         = dynamic_cast<ConfigOptionFloatsNullable *>(printer_preset->config.option("nozzle_diameter"))->values.size();
                    int              extruder_idx           = extruder_id;
                    for (size_t index = 0; index < extruder_count; ++index) {
                        if (physical_extruder_maps[index] == extruder_id) {
                            extruder_idx = index;
                        }
                    }
                    extruder_type = ExtruderType(printer_preset->config.opt_enum("extruder_type", extruder_idx));
                }

                if (extruder_type == ExtruderType::etBowden) {
                    values.push_back(wxString::Format(wxT("%.0f"), 0));
                    values.push_back(wxString::Format(wxT("%.1f"), 0.5));
                    values.push_back(wxString::Format(wxT("%.2f"), 0.05));
                } else {
                    values.push_back(wxString::Format(wxT("%.0f"), 0));
                    values.push_back(wxString::Format(wxT("%.2f"), 0.05));
                    values.push_back(wxString::Format(wxT("%.3f"), 0.005));
                }
                m_custom_range_panel->set_values(values);

                m_custom_range_panel->set_unit("");
                m_custom_range_panel->Show();
            }
        }
    }
    else {
        wxArrayString steps;
        steps.Add(_L("Preset"));
        steps.Add(_L("Calibration"));
        steps.Add(_L("Record Factor"));
        m_step_panel->set_steps_string(steps);
        m_step_panel->set_steps(0);
        if (m_cali_stage_panel)
            m_cali_stage_panel->Show(false);
        if (m_custom_range_panel)
            m_custom_range_panel->Show(false);
        if (m_pa_cali_method_combox)
            m_pa_cali_method_combox->Show(false);
    }
}

void CalibrationPresetPage::on_cali_start_job()
{
    m_sending_panel->reset();
    m_sending_panel->Show();
    Enable_Send_Button(false);
    m_action_panel->show_button(CaliPageActionType::CALI_ACTION_CALI, false);
    Layout();
    Fit();

    m_stop_update_page_status = true;
}

void CalibrationPresetPage::on_cali_finished_job()
{
    m_sending_panel->reset();
    m_sending_panel->Show(false);
    update_print_status_msg(wxEmptyString, false);
    Enable_Send_Button(true);
    m_action_panel->show_button(CaliPageActionType::CALI_ACTION_CALI, true);
    Layout();
    Fit();

    m_stop_update_page_status = false;
}

void CalibrationPresetPage::on_cali_cancel_job()
{
    BOOST_LOG_TRIVIAL(info) << "CalibrationWizard::print_job: enter canceled";
    if (CalibUtils::print_job) {
        if (CalibUtils::print_job->is_running()) {
            BOOST_LOG_TRIVIAL(info) << "calibration_print_job: canceled";
            CalibUtils::print_job->cancel();
        }
        CalibUtils::print_job->join();
    }

    m_sending_panel->reset();
    m_sending_panel->Show(false);
    update_print_status_msg(wxEmptyString, false);
    Enable_Send_Button(true);
    m_action_panel->show_button(CaliPageActionType::CALI_ACTION_CALI, true);
    Layout();
    Fit();

    m_stop_update_page_status = false;
}

void CalibrationPresetPage::init_with_machine(MachineObject* obj)
{
    if (!obj) return;

    //set flow ratio calibration type
    m_cali_stage_panel->set_flow_ratio_calibration_type(obj->GetCalib()->GetFlowRatioCalibType());
    // set nozzle value from machine
    auto diameter = obj->GetExtderSystem()->GetNozzleDiameterType(0);

    if (switch_combox_to_target(m_comboBox_nozzle_dia, diameter)) {
        wxCommandEvent event(wxEVT_COMBOBOX);
        event.SetEventObject(this);
        wxPostEvent(m_comboBox_nozzle_dia, event);
        m_comboBox_nozzle_dia->SetToolTip(_L("The nozzle diameter has been synchronized from the printer Settings"));
    } else {
        m_comboBox_nozzle_dia->SetToolTip(wxEmptyString);
        // set default to 0.4
        switch_combox_to_target(m_comboBox_nozzle_dia, NozzleDiameterType::NOZZLE_DIAMETER_0_4);
    }

    std::map<int, ComboBox*> nozzle_diameter_combox_map {
        {MAIN_EXTRUDER_ID, m_right_comboBox_nozzle_dia},
        {DEPUTY_EXTRUDER_ID, m_left_comboBox_nozzle_dia}
    };

    std::map<int, ComboBox*> nozzle_volume_combox_map {
        {MAIN_EXTRUDER_ID, m_right_comboBox_nozzle_volume},
        {DEPUTY_EXTRUDER_ID, m_left_comboBox_nozzle_volume}
    };

    if (obj->is_multi_extruders()) {
        for (size_t i = 0; i < obj->GetExtderSystem()->GetTotalExtderCount(); ++i) {
            auto diameter = obj->GetExtderSystem()->GetNozzleDiameterType(i);
            if (switch_combox_to_target(nozzle_diameter_combox_map[i], diameter)) {
                wxCommandEvent event(wxEVT_COMBOBOX);
                event.SetEventObject(this);
                wxPostEvent(nozzle_diameter_combox_map[i], event);
                nozzle_diameter_combox_map[i]->SetToolTip(_L("The nozzle diameter has been synchronized from the printer Settings"));
            } else {
                nozzle_diameter_combox_map[i]->SetToolTip(wxEmptyString);
                switch_combox_to_target(nozzle_diameter_combox_map[i], NozzleDiameterType::NOZZLE_DIAMETER_0_4);
            }

            if (obj->GetExtderSystem()->GetNozzleFlowType(i) != NozzleFlowType::NONE_FLOWTYPE) {
                auto volume_type = DevNozzle::ToNozzleVolumeType(obj->GetExtderSystem()->GetNozzleFlowType(i));
                switch_combox_to_target(nozzle_volume_combox_map[i], volume_type);
            } else {
                nozzle_volume_combox_map[i]->SetSelection(0);
            }
        }

        std::string cwp_swap_pt = obj->printer_type;
        // Refresh Nozzle Info box labels with connected printer type
        if (m_left_nozzle_volume_type_sizer) {
            m_left_nozzle_volume_type_sizer->GetStaticBox()->SetLabel(
                _L(DevPrinterConfigUtil::get_toolhead_display_name(cwp_swap_pt, DEPUTY_EXTRUDER_ID, ToolHeadComponent::Nozzle, ToolHeadNameCase::TitleCase)));
        }
        if (m_right_nozzle_volume_type_sizer) {
            m_right_nozzle_volume_type_sizer->GetStaticBox()->SetLabel(
                _L(DevPrinterConfigUtil::get_toolhead_display_name(cwp_swap_pt, MAIN_EXTRUDER_ID, ToolHeadComponent::Nozzle, ToolHeadNameCase::TitleCase)));
        }
        // Refresh Filament For Calibration box labels unconditionally
        if (obj->is_main_extruder_on_left()) {
            m_main_sizer->GetStaticBox()->SetLabel(_L(DevPrinterConfigUtil::get_toolhead_display_name(cwp_swap_pt, DEPUTY_EXTRUDER_ID, ToolHeadComponent::Nozzle, ToolHeadNameCase::TitleCase)));
            m_deputy_sizer->GetStaticBox()->SetLabel(_L(DevPrinterConfigUtil::get_toolhead_display_name(cwp_swap_pt, MAIN_EXTRUDER_ID, ToolHeadComponent::Nozzle, ToolHeadNameCase::TitleCase)));
        } else {
            m_main_sizer->GetStaticBox()->SetLabel(_L(DevPrinterConfigUtil::get_toolhead_display_name(cwp_swap_pt, MAIN_EXTRUDER_ID, ToolHeadComponent::Nozzle, ToolHeadNameCase::TitleCase)));
            m_deputy_sizer->GetStaticBox()->SetLabel(_L(DevPrinterConfigUtil::get_toolhead_display_name(cwp_swap_pt, DEPUTY_EXTRUDER_ID, ToolHeadComponent::Nozzle, ToolHeadNameCase::TitleCase)));
        }
        if (!obj->is_main_extruder_on_left() && m_main_extruder_on_left) {
            m_multi_exturder_ams_sizer->Detach(m_main_filament_cali_panel);
            m_multi_exturder_ams_sizer->Detach(m_deputy_filament_cali_panel);

            m_main_sizer->GetStaticBox()->SetLabel(_L(DevPrinterConfigUtil::get_toolhead_display_name(cwp_swap_pt, MAIN_EXTRUDER_ID, ToolHeadComponent::Nozzle, ToolHeadNameCase::TitleCase)));
            m_deputy_sizer->GetStaticBox()->SetLabel(_L(DevPrinterConfigUtil::get_toolhead_display_name(cwp_swap_pt, DEPUTY_EXTRUDER_ID, ToolHeadComponent::Nozzle, ToolHeadNameCase::TitleCase)));
            m_multi_exturder_ams_sizer->Add(m_deputy_filament_cali_panel, 0, wxEXPAND | wxALL | wxALIGN_BOTTOM, 10);
            m_multi_exturder_ams_sizer->Add(m_main_filament_cali_panel, 0, wxEXPAND | wxALL | wxALIGN_BOTTOM, 10);

            m_main_extruder_on_left = false;
        }
        else if (obj->is_main_extruder_on_left() && !m_main_extruder_on_left) {
            m_multi_exturder_ams_sizer->Detach(m_main_filament_cali_panel);
            m_multi_exturder_ams_sizer->Detach(m_deputy_filament_cali_panel);

            m_main_sizer->GetStaticBox()->SetLabel(_L(DevPrinterConfigUtil::get_toolhead_display_name(cwp_swap_pt, DEPUTY_EXTRUDER_ID, ToolHeadComponent::Nozzle, ToolHeadNameCase::TitleCase)));
            m_deputy_sizer->GetStaticBox()->SetLabel(_L(DevPrinterConfigUtil::get_toolhead_display_name(cwp_swap_pt, MAIN_EXTRUDER_ID, ToolHeadComponent::Nozzle, ToolHeadNameCase::TitleCase)));
            m_multi_exturder_ams_sizer->Add(m_main_filament_cali_panel, 0, wxEXPAND | wxALL | wxALIGN_BOTTOM, 10);
            m_multi_exturder_ams_sizer->Add(m_deputy_filament_cali_panel, 0, wxEXPAND | wxALL | wxALIGN_BOTTOM, 10);

            m_main_extruder_on_left = true;
        }

        m_single_nozzle_info_panel->Hide();
        m_multi_nozzle_info_panel->Show();
        m_multi_exutrder_filament_list_panel->Show();
        m_filament_list_panel->Hide();
    }
    else {

        if ((obj->GetExtderSystem()->GetTotalExtderCount() > 0) && (obj->GetExtderSystem()->GetNozzleFlowType(0) != NozzleFlowType::NONE_FLOWTYPE))
        {
            auto volume_type = DevNozzle::ToNozzleVolumeType(obj->GetExtderSystem()->GetNozzleFlowType(0));
            for(unsigned int i=0; i < m_comboBox_nozzle_volume->GetCount(); i++) {
                if(volume_type == NozzleVolumeType(*(int*)m_comboBox_nozzle_volume->GetClientData(i))) {
                    m_comboBox_nozzle_volume->SetSelection(i);
                    break;
                }
            }
        } else {
            m_comboBox_nozzle_volume->SetSelection(0);
        }

        m_single_nozzle_info_panel->Show();
        m_multi_nozzle_info_panel->Hide();
        m_multi_exutrder_filament_list_panel->Hide();
        m_filament_list_panel->Show();
    }

    Layout();

    // init filaments for calibration
    sync_ams_info(obj);
}

void CalibrationPresetPage::sync_ams_info(MachineObject* obj)
{
    if (!obj) return;
    // read ams slot info from printer, then save to DynamicPrintConfig
    std::map<int, DynamicPrintConfig> old_full_filament_ams_list = build_filament_ams_list(obj);
    std::map<int, DynamicPrintConfig> full_filament_ams_list;
    for (auto ams_item : old_full_filament_ams_list) {
        int key = ams_item.first & 0x0FFFF;
        if (key == VIRTUAL_TRAY_MAIN_ID || key == VIRTUAL_TRAY_DEPUTY_ID) {
            ams_item.second.set_key_value("filament_exist", new ConfigOptionBools{true});
        }
        full_filament_ams_list[key] = std::move(ams_item.second);
    }

    // sync m_filament_ams_list from obj ams list
    m_filament_ams_list.clear();
    for (auto& ams_item : obj->GetFilaSystem()->GetAmsList()) {
        for (auto& tray_item: ams_item.second->GetTrays()) {
            try {
                int ams_id  = std::stoi(ams_item.second->GetAmsId());
                int slot_id = std::stoi(tray_item.second->id);
                int tray_id =  obj->GetFilaSystem()->GetTrayIdByAmsSlotId(ams_id, slot_id);

                if (auto it = full_filament_ams_list.find(tray_id); it != full_filament_ams_list.end()) {
                    m_filament_ams_list[tray_id] = it->second;
                }
            } catch(...) {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "invalid ams_id:"<< ams_item.second->GetAmsId() << " or slot_id:" << tray_item.second->id;
            }
        }
    }
    if (!obj->GetFilaSwitch()->IsInstalled()) {
        // init virtual tray info
        if (full_filament_ams_list.find(VIRTUAL_TRAY_MAIN_ID) != full_filament_ams_list.end()) {
            m_filament_ams_list[VIRTUAL_TRAY_MAIN_ID] = full_filament_ams_list[VIRTUAL_TRAY_MAIN_ID];
        }

        if (full_filament_ams_list.find(VIRTUAL_TRAY_DEPUTY_ID) != full_filament_ams_list.end()) {
            m_filament_ams_list[VIRTUAL_TRAY_DEPUTY_ID] = full_filament_ams_list[VIRTUAL_TRAY_DEPUTY_ID];
        }
    } else {
        DynamicPrintConfig empty_config = get_empty_dynamic_print_config();
        m_filament_ams_list[VIRTUAL_TRAY_MAIN_ID] = empty_config;
        m_filament_ams_list[VIRTUAL_TRAY_DEPUTY_ID] = empty_config;
    }

    update_nozzle_id_combox();

    // update filament from panel, display only obj has ams
    // update multi ams panel, display only obj has multi ams
    if (obj->HasAms()) {
        if (obj->is_multi_extruders()) {
            auto ams_list = obj->GetFilaSystem()->GetAmsList();
            {
                auto main_it = std::find_if(ams_list.begin(), ams_list.end(), [](auto ams_item){
                    auto extruder_set = ams_item.second->GetBindedExtruderSet();
                    return extruder_set.find(MAIN_EXTRUDER_ID) != extruder_set.end();
                });
                if (main_it != ams_list.end()) {
                    update_extruder_filament_combobox(ExtruderRole::MAIN_EXTRUDER, main_it->second->GetAmsId());
                } else {
                    update_extruder_filament_combobox(ExtruderRole::MAIN_EXTRUDER, std::to_string(VIRTUAL_TRAY_MAIN_ID));
                }
            }
            {
                auto deputy_it = std::find_if(ams_list.begin(), ams_list.end(), [](auto ams_item){
                    auto extruder_set = ams_item.second->GetBindedExtruderSet();
                    return extruder_set.find(DEPUTY_EXTRUDER_ID) != extruder_set.end();
                });
                if (deputy_it != ams_list.end()) {
                    update_extruder_filament_combobox(ExtruderRole::DEPUTY_EXTRUDER, deputy_it->second->GetAmsId());
                } else {
                    update_extruder_filament_combobox(ExtruderRole::DEPUTY_EXTRUDER, std::to_string(VIRTUAL_TRAY_DEPUTY_ID));
                }
            }
        }
        else {
            if (obj->GetFilaSystem()->GetAmsList().size() > 1) {
                on_switch_ams(obj->GetFilaSystem()->GetAmsList().begin()->first);
            } else {
                if (!obj->GetFilaSystem()->GetAmsList().empty())
                    update_extruder_filament_combobox(ExtruderRole::SINGLE_EXTRUDER, obj->GetFilaSystem()->GetAmsList().begin()->first);
            }
        }
    }
    else {
        if (obj->is_multi_extruders()) {
            update_extruder_filament_combobox(ExtruderRole::MAIN_EXTRUDER, std::to_string(VIRTUAL_TRAY_MAIN_ID));
            update_extruder_filament_combobox(ExtruderRole::DEPUTY_EXTRUDER, std::to_string(VIRTUAL_TRAY_DEPUTY_ID));
        } else {
            update_extruder_filament_combobox(ExtruderRole::SINGLE_EXTRUDER, std::to_string(VIRTUAL_TRAY_MAIN_ID));
        }
    }

    std::vector<AMSinfo> ams_info;
    std::vector<AMSinfo> main_ams_info;
    std::vector<AMSinfo> deputy_ams_info;
    {
        /* add ams info to ams_info_list */
        for (auto &ams : obj->GetFilaSystem()->GetAmsList()) {
            AMSinfo info;
            info.ams_id = ams.first;

            if (ams.second->IsExist() && info.parse_ams_info(obj, ams.second, obj->GetFilaSystem()->IsDetectRemainEnabled(), obj->is_support_ams_humidity)) {
                for (int extruder_id : ams.second->GetBindedExtruderSet()) {
                    if (extruder_id == MAIN_EXTRUDER_ID) {
                        ams_info.push_back(info);
                        main_ams_info.push_back(info);
                    } else {
                        deputy_ams_info.push_back(info);
                    }
                }
            }
        }

        /* add vt_ams info to ams info list*/
        for (const DevAmsTray& vt_tray : obj->vt_slot) {
            AMSinfo     info;
            info.parse_ext_info(obj, vt_tray);
            info.ams_type = DevAmsType::EXT_SPOOL;

            if (vt_tray.id == std::to_string(VIRTUAL_TRAY_MAIN_ID)) {
                ams_info.push_back(info);
                main_ams_info.push_back(info);
            } else if (vt_tray.id == std::to_string(VIRTUAL_TRAY_DEPUTY_ID)) {
                deputy_ams_info.push_back(info);
            } else {
                assert(false);
            }
        }
    }

    /* update ams preview */
    {   //single extuder
        auto ams_items_sizer = create_ams_items_sizer(m_single_ams_preview_panel, m_single_ams_preview_list, ams_info, ExtruderRole::SINGLE_EXTRUDER);
        auto ams_sizer = new wxBoxSizer(wxVERTICAL);
        ams_sizer->Add(ams_items_sizer, 0);
        ams_sizer->AddSpacer(FromDIP(10));

        m_single_ams_preview_panel->SetSizer(ams_sizer);
    }
    {   //multi extuder
        m_main_ams_preview_panel->SetSizer(create_ams_items_sizer(m_main_ams_preview_panel, m_main_ams_preview_list, main_ams_info, ExtruderRole::MAIN_EXTRUDER));
        m_deputy_ams_preview_panel->SetSizer(create_ams_items_sizer(m_deputy_ams_preview_panel, m_deputy_ams_preview_list, deputy_ams_info, ExtruderRole::DEPUTY_EXTRUDER));
    }

    /* display nozzle combobox */
    {
        auto has_rack = obj->GetNozzleSystem()->GetNozzleRack()->IsSupported();
        if (has_rack) {
            m_tips_map["rack"].first = true;
            m_right_comboBox_nozzle_dia->Enable();
            m_right_comboBox_nozzle_volume->Enable();
        } else {
            m_tips_map["rack"].first = false;
            m_right_comboBox_nozzle_dia->Disable();
            m_right_comboBox_nozzle_volume->Disable();
        }

        m_filament_list_tips->SetLabel(get_filament_tips());
    }

    disable_bowden_extuder_auto_dyn_cali(m_main_filament_cali_panel);

    Layout();
}

void CalibrationPresetPage::update_nozzle_id_combox()
{
    MachineObject *obj = get_current_object();

    if (!obj || !obj->GetNozzleSystem()) return;

    auto rack     = obj->GetNozzleSystem()->GetNozzleRack();
    bool has_rack = rack->IsSupported();
    if (has_rack) {
        NozzleDiameterType nozzle_diameter;
        auto dia_sel = m_right_comboBox_nozzle_dia->GetSelection();
        if (dia_sel != wxNOT_FOUND) {
            nozzle_diameter = NozzleDiameterType(*(int*)m_right_comboBox_nozzle_dia->GetClientData(dia_sel));
        } else {
            nozzle_diameter = NozzleDiameterType::NONE_DIAMETER_TYPE;
        }
        std::vector<NozzleFlowType> nozzle_flows;
        auto sel = m_right_comboBox_nozzle_volume->GetSelection();
        if(sel != wxNOT_FOUND) {
            auto volume_type = NozzleVolumeType(*(int*)m_right_comboBox_nozzle_volume->GetClientData(sel));
            if (volume_type != NozzleVolumeType::nvtHybrid)
                nozzle_flows.emplace_back(DevNozzle::ToNozzleFlowType(volume_type));
            else {
                for (auto volume : volumes) {
                    if (volume != NozzleVolumeType::nvtHybrid)
                        nozzle_flows.emplace_back(DevNozzle::ToNozzleFlowType(volume));
                }
            }
        } else {
            nozzle_flows.emplace_back(NozzleFlowType::NONE_FLOWTYPE);
        }

        int  r_nozzle_id = obj->GetExtderSystem()->GetExtderById(MAIN_EXTRUDER_ID)->GetNozzleId();
        auto r_nozzle    = obj->GetNozzleSystem()->GetExtNozzle(r_nozzle_id);
        auto nozzle_map  = rack->GetRackNozzles();

        auto nozzle_list = make_nozzles_info(r_nozzle, nozzle_map, nozzle_diameter, nozzle_flows);
        for (auto &fcb : m_main_filament_comboBox_list) {
            fcb->UpdateNozzleCombo(nozzle_list);
            fcb->ShowNozzleCombo();
        }
    } else {
        for (auto &fcb : m_main_filament_comboBox_list) {
            fcb->HideNozzleCombo();
        }
    }
}

std::vector<std::pair<wxString, int>> CalibrationPresetPage::make_nozzles_info(const DevNozzle                      &r_nozzle,
                                                                               const std::map<int, DevNozzle>       &nozzle_map,
                                                                               const NozzleDiameterType             &nozzle_diameter,
                                                                               const std::vector<NozzleFlowType>    &nozzle_flows)
{
    std::vector<std::pair<wxString, int>> nozzle_list;

    bool exist = std::find(nozzle_flows.begin(), nozzle_flows.end(), r_nozzle.GetNozzleFlowType()) != nozzle_flows.end();
    if (r_nozzle.IsNormal() && r_nozzle.GetNozzleDiameterType() == nozzle_diameter && exist) {
        wxString item = wxString::Format("R | %s %s", r_nozzle.GetNozzleDiameterStr(), r_nozzle.GetNozzleFlowTypeStr());
        nozzle_list.emplace_back(item, 0);
    }
    for (auto &nozzle : nozzle_map) {
        exist = std::find(nozzle_flows.begin(), nozzle_flows.end(), nozzle.second.GetNozzleFlowType()) != nozzle_flows.end();
        if (nozzle.second.IsNormal() && nozzle.second.GetNozzleDiameterType() == nozzle_diameter && exist) {
            wxString item = wxString::Format("%d | %s %s", nozzle.second.GetNozzleId() + 1, nozzle.second.GetNozzleDiameterStr(), nozzle.second.GetNozzleFlowTypeStr());
            nozzle_list.emplace_back(item, 0x10 | nozzle.second.GetNozzleId());
        }
    }

    return nozzle_list;
}

void CalibrationPresetPage::disable_bowden_extuder_auto_dyn_cali(wxWindow* cali_panel){
    // only support one extruder is bowden
    if ((m_cali_method == CalibrationMethod::CALI_METHOD_AUTO || m_cali_method == CalibrationMethod::CALI_METHOD_NEW_AUTO) && get_extruder_type(MAIN_EXTRUDER_ID) == ExtruderType::etBowden) {
        m_tips_map["bowden_left"].first = false;
        m_tips_map["bowden_right"].first = true;
        cali_panel->Disable();
    } else if ((m_cali_method == CalibrationMethod::CALI_METHOD_AUTO || m_cali_method == CalibrationMethod::CALI_METHOD_NEW_AUTO) && get_extruder_type(DEPUTY_EXTRUDER_ID) == ExtruderType::etBowden) {
        m_tips_map["bowden_left"].first = true;
        m_tips_map["bowden_right"].first = false;
        cali_panel->Disable();
    } else {
        m_tips_map["bowden_left"].first = false;
        m_tips_map["bowden_right"].first = false;
        cali_panel->Enable();
    }

    m_filament_list_tips->SetLabel(get_filament_tips());
}

void CalibrationPresetPage::select_default_compatible_filament()
{
    if (!curr_obj)
        return;

    if (curr_obj->is_multi_extruders()) {
        auto select_first_compatible_filament = [this](FilamentComboBoxList& filament_list) -> bool {
            for (auto &fcb : filament_list) {
                if (!fcb || !fcb->GetRadioBox() || !fcb->GetRadioBox()->IsEnabled()) {
                    continue;
                }

                int tray_id = fcb->get_tray_id();
                if (tray_id < 0) {
                    fcb->SetValue(false);
                    continue;
                }

                Preset* preset = const_cast<Preset*>(fcb->GetComboBox()->get_selected_preset());
                if (!preset) {
                    fcb->SetValue(false);
                    continue;
                }

                int nozzle_pos_id = fcb->GetNozzleIdCode();
                std::string nozzle_sn;
                if (nozzle_pos_id != -1) {
                    DevNozzle nozzle = curr_obj->get_nozzle_by_id_code(nozzle_pos_id);
                    nozzle_sn = nozzle.GetSerialNumber().ToStdString();
                }

                int extruder_id = fcb->GetExtuderRole() == ExtruderRole::DEPUTY_EXTRUDER ? DEPUTY_EXTRUDER_ID : MAIN_EXTRUDER_ID;
                std::vector<CaliFilamentInfo> selected_filament{
                    CaliFilamentInfo(preset, nozzle_pos_id, nozzle_sn, extruder_id, tray_id)
                };

                if (is_filaments_compatiable(selected_filament)) {
                    manage_filament_radio_btn(fcb);

                    wxCommandEvent event(wxEVT_TOGGLEBUTTON);
                    event.SetEventObject(this);
                    wxPostEvent(fcb->GetRadioBox(), event);
                    Layout();
                    return true;
                }

                fcb->SetValue(false);
            }

            return false;
        };

        if (!select_first_compatible_filament(m_main_filament_comboBox_list))
            select_first_compatible_filament(m_deputy_filament_comboBox_list);

        check_filament_compatible();
        return;
    }

    std::string ams_id;
    for (AMSPreview* ams_perview : m_single_ams_preview_list) {
        if (ams_perview->IsSelected()) {
            ams_id = ams_perview->get_ams_id();
            break;
        }
    }

    if (ams_id.empty())
        return;

    if (!devPrinterUtil::IsVirtualSlot(ams_id)) {
        std::vector<CaliFilamentInfo> selected_filament;
        for (size_t i = 0; i < MAX_SLOT_NUM; ++i) {
            auto &fcb = m_filament_comboBox_list[i];
            if (!fcb->GetRadioBox()->IsEnabled())
                continue;
            int tray_id = fcb->get_tray_id();
            int nozzle_pos_id = fcb->GetNozzleIdCode();
            int extruder_id = fcb->GetExtuderRole() == ExtruderRole::DEPUTY_EXTRUDER ? DEPUTY_EXTRUDER_ID : MAIN_EXTRUDER_ID;
            std::string nozzle_sn;
            if (nozzle_pos_id != -1) {
                DevNozzle nozzle = curr_obj->get_nozzle_by_id_code(nozzle_pos_id);
                nozzle_sn        = nozzle.GetSerialNumber().ToStdString();
            }
            Preset* preset = const_cast<Preset *>(fcb->GetComboBox()->get_selected_preset());
            if (m_cali_filament_mode == CalibrationFilamentMode::CALI_MODEL_SINGLE) {
                selected_filament.clear();
                CaliFilamentInfo info;
                info.tray_id         = tray_id;
                info.filament_preset = preset;
                info.nozzle_pos_id   = nozzle_pos_id;
                info.nozzle_sn       = nozzle_sn;
                info.extruder_id     = extruder_id;
                selected_filament.emplace_back(info);
                if (preset && is_filaments_compatiable(selected_filament)) {
                    fcb->GetRadioBox()->SetValue(true);
                    wxCommandEvent event(wxEVT_TOGGLEBUTTON);
                    event.SetEventObject(this);
                    wxPostEvent(fcb->GetRadioBox(), event);
                    Layout();
                    break;
                } else
                    fcb->GetRadioBox()->SetValue(false);
            } else if (m_cali_filament_mode == CalibrationFilamentMode::CALI_MODEL_MULITI) {
                if (!preset) {
                    fcb->GetCheckBox()->SetValue(false);
                    continue;
                }
                if (!is_filaments_compatiable(selected_filament)) {
                    fcb->GetCheckBox()->SetValue(false);
                } else {
                    selected_filament.emplace_back(CaliFilamentInfo(preset, nozzle_pos_id, nozzle_sn, extruder_id, tray_id));
                    fcb->GetCheckBox()->SetValue(true);
                }

                wxCommandEvent event(wxEVT_CHECKBOX);
                event.SetEventObject(this);
                wxPostEvent(fcb->GetCheckBox(), event);
                Layout();
            }
        }
    }
    else {
        CaliFilamentInfo selected_fila_info;
        Preset  *preset  = const_cast<Preset *>(m_filament_comboBox_list[0]->GetComboBox()->get_selected_preset());
        int tray_id = m_filament_comboBox_list[0]->get_tray_id();

        selected_fila_info.tray_id         = tray_id;
        selected_fila_info.filament_preset = preset;
        selected_fila_info.nozzle_pos_id   = m_filament_comboBox_list[0]->GetNozzleIdCode();

        std::string nozzle_sn;
        if (selected_fila_info.nozzle_pos_id == -1) {
            DevNozzle nozzle = curr_obj->get_nozzle_by_id_code(selected_fila_info.nozzle_pos_id);
            nozzle_sn                    = nozzle.GetSerialNumber().ToStdString();
        }
        if (preset && is_filaments_compatiable({selected_fila_info})) {
            m_filament_comboBox_list[0]->GetRadioBox()->SetValue(true);
        } else {
            m_filament_comboBox_list[0]->GetRadioBox()->SetValue(false);
        }

        wxCommandEvent event(wxEVT_TOGGLEBUTTON);
        event.SetEventObject(this);
        wxPostEvent(m_filament_comboBox_list[0]->GetRadioBox(), event);
        Layout();
    }

    check_filament_compatible();
}

int CalibrationPresetPage::get_index_by_extruder_tray_id(int extruder_id, int tray_id)
{
    std::vector<FilamentComboBox*> fcb_list = get_selected_filament_combobox();
    for (auto fcb : fcb_list) {
        int fcb_ext_id = fcb->GetExtuderRole() != ExtruderRole::DEPUTY_EXTRUDER ? MAIN_EXTRUDER_ID : DEPUTY_EXTRUDER_ID;
        if (fcb_ext_id == extruder_id && fcb->get_tray_id() == tray_id) {
            return fcb->get_index();
        }
    }
    return -1;
}

std::vector<FilamentComboBox*> CalibrationPresetPage::get_selected_filament_combobox()
{
    std::vector<FilamentComboBox*> fcb_list;

    auto is_valid_selected_filament = [](FilamentComboBox* fcb) {
        return fcb
            && fcb->GetComboBox()
            && fcb->get_tray_id() >= 0
            && fcb->GetComboBox()->get_selected_preset();
    };

    if (curr_obj && curr_obj->is_multi_extruders()) {
        if (m_cali_filament_mode == CalibrationFilamentMode::CALI_MODEL_MULITI) {
            for (auto &fcb : m_main_filament_comboBox_list) {
                if (fcb->GetCheckBox()->GetValue()) {
                    if (is_valid_selected_filament(fcb))
                        fcb_list.push_back(fcb);
                    else
                        fcb->SetValue(false);
                }
            }
            for (auto &fcb : m_deputy_filament_comboBox_list) {
                if (fcb->GetCheckBox()->GetValue()) {
                    if (is_valid_selected_filament(fcb))
                        fcb_list.push_back(fcb);
                    else
                        fcb->SetValue(false);
                }
            }
        } else if (m_cali_filament_mode == CalibrationFilamentMode::CALI_MODEL_SINGLE) {
            auto append_selected_radio = [&fcb_list, &is_valid_selected_filament](FilamentComboBoxList& list) {
                for (auto &fcb : list) {
                    if (!fcb || !fcb->GetRadioBox() || !fcb->GetRadioBox()->GetValue())
                        continue;

                    if (!fcb->GetRadioBox()->IsEnabled() || !is_valid_selected_filament(fcb)) {
                        fcb->SetValue(false);
                        continue;
                    }

                    if (fcb_list.empty())
                        fcb_list.push_back(fcb);
                    else
                        fcb->SetValue(false);
                }
            };

            append_selected_radio(m_main_filament_comboBox_list);
            append_selected_radio(m_deputy_filament_comboBox_list);
        }
    }
    else {
        if (m_cali_filament_mode == CalibrationFilamentMode::CALI_MODEL_MULITI) {
            for (auto &fcb : m_filament_comboBox_list) {
                if (fcb->GetCheckBox()->GetValue() && is_valid_selected_filament(fcb)) {
                    fcb_list.push_back(fcb);
                }
            }
        } else if (m_cali_filament_mode == CalibrationFilamentMode::CALI_MODEL_SINGLE) {
            for (auto &fcb : m_filament_comboBox_list) {
                if (fcb->GetRadioBox()->GetValue() && fcb->GetRadioBox()->IsEnabled() && is_valid_selected_filament(fcb)) {
                    fcb_list.push_back(fcb);
                }
            }
        }
    }

    return fcb_list;
}

std::vector<CaliFilamentInfo> CalibrationPresetPage::get_selected_filaments()
{
    std::vector<CaliFilamentInfo> out;
    if (!curr_obj || !curr_obj->GetNozzleSystem()) return out;

    std::vector<FilamentComboBox*> fcb_list = get_selected_filament_combobox();
    for (auto &fcb : fcb_list) {
        int nozzle_pos_id = -1;
        std::string nozzle_sn;

        if (curr_obj->GetNozzleSystem()->GetNozzleRack()->IsSupported())
        {
            if (fcb->GetExtuderRole()== ExtruderRole::MAIN_EXTRUDER)
            {
                nozzle_pos_id = fcb->GetNozzleIdCode();
                if (nozzle_pos_id != -1) {
                    DevNozzle nozzle = curr_obj->get_nozzle_by_id_code(nozzle_pos_id);
                    nozzle_sn = nozzle.GetSerialNumber().ToStdString();
                } else {
                    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__<< "rack: right extuder has an invaild pos id";
                }
            }
            else if (fcb->GetExtuderRole()== ExtruderRole::DEPUTY_EXTRUDER)
            {
                nozzle_sn = "";
                nozzle_pos_id = DEPUTY_EXTRUDER_ID;
            }
        }
        // valid tray id
        if (fcb->get_tray_id() >= 0)
        {
            Preset* preset = const_cast<Preset*>(fcb->GetComboBox()->get_selected_preset());
            if (!preset)
                continue;

            if (nozzle_pos_id == -1) {//non-O1C printer pos_id == extruder_id
                if(fcb->GetExtuderRole() == ExtruderRole::MAIN_EXTRUDER || fcb->GetExtuderRole() == ExtruderRole::SINGLE_EXTRUDER)
                    nozzle_pos_id = MAIN_EXTRUDER_ID;
                else if(fcb->GetExtuderRole() == ExtruderRole::DEPUTY_EXTRUDER)
                    nozzle_pos_id = DEPUTY_EXTRUDER_ID;
            }
            // fcb position in UI left or right
            int extruder_id = fcb->GetExtuderRole() == ExtruderRole::DEPUTY_EXTRUDER ? DEPUTY_EXTRUDER_ID : MAIN_EXTRUDER_ID;
            out.emplace_back(CaliFilamentInfo(preset, nozzle_pos_id, nozzle_sn, extruder_id, fcb->get_tray_id()));
        }
    }

    return out;
}

void CalibrationPresetPage::get_preset_info(float& nozzle_dia, BedType& plate_type)
{
    auto sel = m_comboBox_nozzle_dia->GetSelection();
    if (sel != wxNOT_FOUND) {
        auto diameter = NozzleDiameterType(*(int*)m_comboBox_nozzle_dia->GetClientData(sel));
        nozzle_dia = DevNozzle::ToNozzleDiameterFloat(diameter);
    } else {
        nozzle_dia = -1.0f;
    }

    if (m_comboBox_bed_type->GetSelection() >= 0)
        plate_type = static_cast<BedType>(m_displayed_bed_types[m_comboBox_bed_type->GetSelection()]);
}

void CalibrationPresetPage::get_cali_stage(CaliPresetStage& stage, float& value)
{
    m_cali_stage_panel->get_cali_stage(stage, value);

    if (stage != CaliPresetStage::CALI_MANUAL_STAGE_2) {
        std::vector<CaliFilamentInfo> selected_filaments = get_selected_filaments();
        if (!selected_filaments.empty()) {
            Preset* preset = selected_filaments.begin()->filament_preset;
            const ConfigOptionFloatsNullable* flow_ratio_opt = preset ? preset->config.option<ConfigOptionFloatsNullable>("filament_flow_ratio") : nullptr;
            if (flow_ratio_opt) {
                m_cali_stage_panel->set_flow_ratio_value(flow_ratio_opt->get_at(0));
                value = flow_ratio_opt->get_at(0);
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " set flow ratio value:" << value;
            }
            else {
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " selected filament preset has no flow ratio option";
            }
        } else {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " no filament selected, can't set flow ratio value";
        }
    }
}

// render an ams info to ui by ams_id
void CalibrationPresetPage::update_slots_panel(FilamentComboBoxList& fila_combox_list, const std::string& ams_id, std::map<int, DynamicPrintConfig>& fila_ams_list)
{
    if (!curr_obj) return;

    int int_ams_id = -1;
    try {
        int_ams_id = std::stoi(ams_id);
    } catch(...) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "invalid ams_id:"<< ams_id;
        return;
    }

    int slot_size = MAX_SLOT_NUM;
    auto ams_item = curr_obj->GetFilaSystem()->GetAmsById(ams_id);
    if (ams_item) {
        slot_size = ams_item->GetSlotCount();
    } else if (ams_id == std::to_string(VIRTUAL_TRAY_MAIN_ID) || ams_id == std::to_string(VIRTUAL_TRAY_DEPUTY_ID)) {
        slot_size = 1;
    }

    for (int slot_id = 0; slot_id < MAX_SLOT_NUM; slot_id++) {
        fila_combox_list[slot_id]->SetValue(false);
        if (slot_id < slot_size) {
            fila_combox_list[slot_id]->ShowPanel();
        } else {
            fila_combox_list[slot_id]->HidePanel();
        }

        int tray_index = curr_obj->GetFilaSystem()->GetTrayIdByAmsSlotId(int_ams_id, slot_id);

        if (!fila_combox_list.empty()) {
            auto it = std::find_if(fila_ams_list.begin(), fila_ams_list.end(), [tray_index](auto &entry) { return entry.first == tray_index; });
            if (it != fila_ams_list.end()) {
                fila_combox_list[slot_id]->load_tray_from_ams(tray_index, it->second);
            } else {
                fila_combox_list[slot_id]->load_tray_from_ams(tray_index, get_empty_dynamic_print_config());
            }
        }
    }
}

void CalibrationPresetPage::update_extruder_filament_combobox(ExtruderRole role, const std::string &ams_id)
{
    std::map<ExtruderRole, FilamentComboBoxList*> filament_combox_map{
        {ExtruderRole::SINGLE_EXTRUDER, &m_filament_comboBox_list},
        {ExtruderRole::MAIN_EXTRUDER,   &m_main_filament_comboBox_list},
        {ExtruderRole::DEPUTY_EXTRUDER, &m_deputy_filament_comboBox_list}
    };

    std::map<ExtruderRole, std::vector<AMSPreview *>*> ams_prevew_map{
        {ExtruderRole::SINGLE_EXTRUDER, &m_single_ams_preview_list},
        {ExtruderRole::MAIN_EXTRUDER,   &m_main_ams_preview_list},
        {ExtruderRole::DEPUTY_EXTRUDER, &m_deputy_ams_preview_list}
    };

    for (auto &fcb : *filament_combox_map[role]) {
        fcb->update_from_preset();
        fcb->set_select_mode(m_cali_filament_mode);
    }

    for (auto item : *ams_prevew_map[role]) {
        if (item->get_ams_id() == ams_id) {
            item->OnSelected();
        } else {
            item->UnSelected();
        }
    }

    if (m_filament_ams_list.empty())
        return;
    else
        update_slots_panel(*filament_combox_map[role], ams_id, m_filament_ams_list);

    Layout();
}

Preset* CalibrationPresetPage::get_printer_preset(MachineObject* obj, float nozzle_value)
{
    if (!obj) return nullptr;

    Preset* printer_preset = nullptr;
    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    for (auto printer_it = preset_bundle->printers.begin(); printer_it != preset_bundle->printers.end(); printer_it++) {
        // only use system printer preset
        if (!printer_it->is_system) continue;

        ConfigOption* printer_nozzle_opt = printer_it->config.option("nozzle_diameter");
        ConfigOptionFloatsNullable *printer_nozzle_vals = nullptr;
        if (printer_nozzle_opt)
            printer_nozzle_vals = dynamic_cast<ConfigOptionFloatsNullable*>(printer_nozzle_opt);
        std::string model_id = printer_it->get_current_printer_type(preset_bundle);

        std::string printer_type = obj->printer_type;
        if (obj->is_support_upgrade_kit && obj->installed_upgrade_kit) { printer_type = "C12"; }
        if (model_id.compare(printer_type) == 0
            && printer_nozzle_vals
            && abs(printer_nozzle_vals->get_at(0) - nozzle_value) < 1e-3) {
            printer_preset = &(*printer_it);
        }
    }

    if (!printer_preset) {
        BOOST_LOG_TRIVIAL(warning) << "get_printer_preset: no matching system printer preset, printer_type="
                                   << obj->printer_type << " nozzle_value=" << nozzle_value;
    }

    return printer_preset;
}

Preset* CalibrationPresetPage::get_print_preset()
{
    Preset* printer_preset = get_printer_preset(curr_obj, get_nozzle_value());

    Preset* print_preset = nullptr;
    wxArrayString print_items;

    // get default print profile
    std::string default_print_profile_name;
    if (printer_preset && printer_preset->config.has("default_print_profile")) {
        default_print_profile_name = printer_preset->config.opt_string("default_print_profile");
    }

    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle) {
        for (auto print_it = preset_bundle->prints.begin(); print_it != preset_bundle->prints.end(); print_it++) {
            if (print_it->name == default_print_profile_name) {
                print_preset = &(*print_it);
                BOOST_LOG_TRIVIAL(trace) << "CaliPresetPage: get_print_preset = " << print_preset->name;
            }
        }
    }

    if (!print_preset) {
        BOOST_LOG_TRIVIAL(warning) << "get_print_preset: null, printer_preset=" << (printer_preset ? "found" : "null")
                                   << " default_print_profile=\"" << default_print_profile_name << "\"";
    }

    return print_preset;
}

std::string CalibrationPresetPage::get_print_preset_name()
{
    Preset* print_preset = get_print_preset();
    if (print_preset)
        return print_preset->name;
    return "";
}

wxArrayString CalibrationPresetPage::get_custom_range_values()
{
    if (m_custom_range_panel) {
        return m_custom_range_panel->get_values();
    }
    return wxArrayString();
}

CalibMode CalibrationPresetPage::get_pa_cali_method()
{
    if (m_pa_cali_method_combox) {
        int selected_mode = m_pa_cali_method_combox->get_selection();
        if (selected_mode == PA_LINE) {
            return CalibMode::Calib_PA_Line;
        }
        else if (selected_mode == PA_PATTERN) {
            return CalibMode::Calib_PA_Pattern;
        }
    }
    return CalibMode::Calib_PA_Line;
}

MaxVolumetricSpeedPresetPage::MaxVolumetricSpeedPresetPage(
    wxWindow *parent, CalibMode cali_mode, bool custom_range, wxWindowID id, const wxPoint &pos, const wxSize &size, long style)
    : CalibrationPresetPage(parent, cali_mode, custom_range, id, pos, size, style)
{
    if (custom_range && m_custom_range_panel) {
        wxArrayString titles;
        titles.push_back(_L("From Volumetric Speed"));
        titles.push_back(_L("To Volumetric Speed"));
        titles.push_back(_L("Step"));
        m_custom_range_panel->set_titles(titles);

        m_custom_range_panel->set_unit("mm³/s");
    }
}
}}
