#include <iostream>
#include <gtkmm.h>
#include <string_view>

#include "hw_list.hpp"

namespace
{
Gtk::Window* pMainWindow = nullptr;
Glib::RefPtr<Gtk::Application> app;

void hardware_window_process(Glib::RefPtr<Gtk::Builder> pBuilder)
{
    static size_t selected_item = GTK_INVALID_LIST_POSITION;

    auto getWidget = [pBuilder]<typename T>(std::string_view name) {
        auto widget = pBuilder->get_widget<T>(name.data());
        if(widget == nullptr) {
            std::cerr << "No widget " << name << std::endl;
        }
        return widget;
    };

    auto getObject = [pBuilder]<typename T>(std::string_view name) {
        auto widget = pBuilder->get_object<T>(name.data());
        if(widget == nullptr) {
            std::cerr << "No object " << name << std::endl;
        }
        return widget;
    };

    auto draw_selected = [getObject, getWidget](guint pos) {
        auto wname = getWidget.template operator()<Gtk::Entry>("hw_entry_name");
        auto want = getWidget.template operator()<Gtk::SpinButton>("hw_antennas_sb");
        auto wsub = getWidget.template operator()<Gtk::SpinButton>("hw_subcar_sb");
        auto wdesc = getObject.template operator()<Gtk::TextBuffer>("hw_desc_buffer");

        Hardware::Settings curSettings;
        try {
            curSettings = HW_List::get_instance().get_hardware(pos).get_settings();
        }
        catch(const std::range_error& ex) {
            std::cerr << ex.what() << std::endl;
            return;
        }
        wname->get_buffer()->set_text(curSettings.name);
        want->set_value(curSettings.antennas);
        wsub->set_value(curSettings.sub_cars);
        wdesc->set_text(curSettings.description);
    };

    auto get_and_draw_selected = [pBuilder, getObject, draw_selected]() {
        auto hw_selector = getObject.template operator()<Gtk::SingleSelection>("hw_selector");
        hw_selector->set_model(Gtk::StringList::create(HW_List::get_instance().get_names()));
        if(selected_item != GTK_INVALID_LIST_POSITION)
            draw_selected(selected_item);
    };

    HW_List::get_instance().signal_update().connect(get_and_draw_selected);

    auto hw_list_view = getWidget.template operator()<Gtk::ListView>("hw_list_view");
    hw_list_view->signal_activate().connect([&selected_item, draw_selected](size_t pos) {
        selected_item = pos;
        draw_selected(pos);
    });

    auto new_bt = getWidget.template operator()<Gtk::Button>("hw_new_button");
    new_bt->signal_clicked().connect([](){
        HW_List::get_instance().add_hardware({"Новое оборудование", "", 0, 0});
    });

    auto update_list = [getObject]() {
        auto hw_selector = getObject.template operator()<Gtk::SingleSelection>("hw_selector");
        hw_selector->set_model(Gtk::StringList::create(HW_List::get_instance().get_names()));
    };

    auto apply_bt = getWidget.template operator()<Gtk::Button>("hw_apply_button");
    apply_bt->signal_clicked().connect([getObject, getWidget, update_list](){
        auto wname = getWidget.template operator()<Gtk::Entry>("hw_entry_name");
        auto want = getWidget.template operator()<Gtk::SpinButton>("hw_antennas_sb");
        auto wsub = getWidget.template operator()<Gtk::SpinButton>("hw_subcar_sb");
        auto wdesc = getObject.template operator()<Gtk::TextBuffer>("hw_desc_buffer");

        Hardware::Settings curSettings;
        curSettings.name = wname->get_buffer()->get_text();
        curSettings.antennas = want->get_value();
        curSettings.sub_cars = wsub->get_value();
        curSettings.description = wdesc->get_text();

        if(selected_item != GTK_INVALID_LIST_POSITION)
            HW_List::get_instance().get_hardware(selected_item).set_settings(curSettings);
    });

    auto cancel_bt = getWidget.template operator()<Gtk::Button>("hw_cancel_button");
    cancel_bt->signal_clicked().connect([get_and_draw_selected]() {
        get_and_draw_selected();
    });

    auto delete_bt = getWidget.template operator()<Gtk::Button>("hw_delete_button");
    delete_bt->signal_clicked().connect([&selected_item, update_list, getObject, getWidget]() {
        if(selected_item != GTK_INVALID_LIST_POSITION) {
            size_t buf = selected_item;
            selected_item = GTK_INVALID_LIST_POSITION;
            HW_List::get_instance().delete_hardware(buf);

            getWidget.template operator()<Gtk::Entry>("hw_entry_name")->get_buffer()->set_text("");
            getWidget.template operator()<Gtk::SpinButton>("hw_antennas_sb")->set_value(0);
            getWidget.template operator()<Gtk::SpinButton>("hw_subcar_sb")->set_value(0);
            getObject.template operator()<Gtk::TextBuffer>("hw_desc_buffer")->set_text("");
        }
    });

    update_list();
}

void main_window_process(Glib::RefPtr<Gtk::Builder> pBuilder)
{
    //auto glarea_box = refBuilder->get_widget<Gtk::Box>("main_glarea_box");
    auto conButtonWindow = [pBuilder](std::string_view bname, std::string_view wname) {
        auto button = pBuilder->get_widget<Gtk::Button>(bname.data());
        if(button == nullptr) {
            std::cerr << "No button " << bname;
            return;
        }

        auto window = pBuilder->get_widget<Gtk::Window>(wname.data());
        if(window == nullptr) {
            std::cerr << "No window " << wname;
            return;
        }

        button->signal_clicked().connect(
            [button, window]() {
                window->set_visible(true);
            }
        );
    };

    conButtonWindow("exp_window_button", "experiment_window");
    conButtonWindow("hw_window_button", "hw_window");
    conButtonWindow("import_window_button", "import_window");

    //conButtonWindow("export_window_button", "");
    //conButtonWindow("handler_window_button", "");
}

void on_app_activate()
{
  // Load the GtkBuilder file and instantiate its widgets:
  auto refBuilder = Gtk::Builder::create();
  try
  {
    refBuilder->add_from_file("db_collector.ui");
  }
  catch(const Glib::FileError& ex)
  {
    std::cerr << "FileError: " << ex.what() << std::endl;
    return;
  }
  catch(const Glib::MarkupError& ex)
  {
    std::cerr << "MarkupError: " << ex.what() << std::endl;
    return;
  }
  catch(const Gtk::BuilderError& ex)
  {
    std::cerr << "BuilderError: " << ex.what() << std::endl;
    return;
  }

  // Get the GtkBuilder-instantiated dialog:
  pMainWindow = refBuilder->get_widget<Gtk::Window>("main_window");
  if (!pMainWindow)
  {
    std::cerr << "Could not get the dialog" << std::endl;
    return;
  }


  pMainWindow->signal_hide().connect([] () { delete pMainWindow; });
  auto glarea_box = refBuilder->get_widget<Gtk::Box>("main_glarea_box");
  auto glarea = refBuilder->get_widget<Gtk::GLArea>("graph_printer");
  //glarea_box->remove(*glarea);
  //glarea_box->prepend(*Gtk::make_managed<Gtk::Label>("BEBRA"));



  app->add_window(*pMainWindow);
  pMainWindow->set_visible(true);

  main_window_process(refBuilder);
  hardware_window_process(refBuilder);
}
} // anonymous namespace

int main(int argc, char** argv)
{
  app = Gtk::Application::create("org.gtkmm.example");

  // Instantiate a dialog when the application has been activated.
  // This can only be done after the application has been registered.
  // It's possible to call app->register_application() explicitly, but
  // usually it's easier to let app->run() do it for you.
  app->signal_activate().connect([] () { on_app_activate(); });

  return app->run(argc, argv);
}
