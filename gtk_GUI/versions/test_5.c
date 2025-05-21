#include <termios.h>
#include <fcntl.h>

#include <gtk/gtk.h>

int32_t SERIAL_ID;

// Define GIMP-like dark colors
#define BG_COLOR "#323232"
#define TEXT_COLOR "#f0f0f0" 
#define BUTTON_BG "#424242"
#define BUTTON_HOVER "#5a5a5a"
#define TERMINAL_BG "#252525"
#define TERMINAL_TEXT "#e0e0e0"
#define ACCENT_COLOR "#ff8800"

int32_t terminal_init(int32_t serial)
{
    if (serial < 0) { return -1; }
    struct termios options;

    if(tcgetattr(serial, &options) < 0) { return -1; }

    cfsetispeed(&options, B9600);
    cfsetospeed(&options, B9600);

    options.c_iflag &= ~BRKINT;
    options.c_iflag &= ~IMAXBEL;
    options.c_lflag &= ~ECHO;

    if (tcsetattr(serial, TCSANOW, &options) < 0) { return -1; }
    return 0;
}


static void apply_css(void)
{
  GtkCssProvider *provider = gtk_css_provider_new();
  
  const char *css =
    "textview {"
    "  background-color: " TERMINAL_BG ";"
    "  color: " TERMINAL_TEXT ";"
    "  font-family: monospace;"
    "}"
    "scrolledwindow {"
    "  border: 1px solid #121212;"
    "  border-radius: 2px;"
    "}"
    ;
  
  gtk_css_provider_load_from_data(provider, css, -1);
  gtk_style_context_add_provider_for_display(
    gdk_display_get_default(),
    GTK_STYLE_PROVIDER(provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
  );
  
  g_object_unref(provider);
}

static void send_data_popup(GtkWidget *widget, gpointer data)
{
  GtkApplication *app = GTK_APPLICATION(data);
  GtkWidget *popup;
  GtkWidget *label;
 
  popup = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(popup), "Popup");
  gtk_window_set_default_size(GTK_WINDOW(popup), 200, 200);
  gtk_window_set_resizable(GTK_WINDOW(popup), FALSE);
  
  label = gtk_label_new("Hmmmmm");
  gtk_widget_add_css_class(label, "white-text");
  gtk_window_set_child(GTK_WINDOW(popup), label);
  gtk_window_present(GTK_WINDOW(popup));
}


// TODO: too much buffer checks, add some bool
// TODO: add bool that checks if connected, if not connected locks all buttons
// TODO: if one connect popup open second one cannot open, probably with bool, do some flags
static void connect_entry(GtkEntry *entry, gpointer *data)
{
  const gchar *device = gtk_editable_get_text(GTK_EDITABLE(entry));
  GtkTextIter iter;
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER(data);

  // TODO: remove g_strcmp0 +() since serial will do connection check, but add strcat("/dev", device);
  if(!g_strcmp0(device, "ttyACM0")) {
    GtkRoot *popup = gtk_widget_get_root(GTK_WIDGET(entry));
    if(GTK_WINDOW(popup)) {
      SERIAL_ID = open("/dev/ttyACM0", O_RDWR);
      if(SERIAL_ID < 0) {
        g_warning("Error while opening serial port\n");
        if (buffer) {
          gtk_text_buffer_get_end_iter(buffer, &iter);
          gtk_text_buffer_insert(buffer, &iter, "ERROR: Failed to open serial port\n", -1);
        }
      }
      if (terminal_init(SERIAL_ID)) {
        if(buffer) {
          gtk_text_buffer_get_end_iter(buffer, &iter);
          gtk_text_buffer_insert(buffer, &iter, "ERROR: Failed to setup terminal connection!\n", -1);
        }
        g_warning("Error while setting up terminal connection\n");
      }
      else {
        if (buffer) {
          gtk_text_buffer_get_end_iter(buffer, &iter);
          gtk_text_buffer_insert(buffer, &iter, "Successfully connected to device: ", -1);
          gtk_text_buffer_insert(buffer, &iter, device, -1);
          gtk_text_buffer_insert(buffer, &iter, "\n", -1);
        }
        gtk_window_destroy(GTK_WINDOW(popup));
        g_print("Successfully connected to device: %s\n", device);
      }
    }
  }

}

static void disconnect_device(GtkWidget *widget, gpointer data)
{
  GtkTextBuffer *buffer = g_object_get_data(G_OBJECT(widget), "buffer");
  GtkTextIter iter;

  close(SERIAL_ID);
  gtk_text_buffer_get_end_iter(buffer, &iter);
  gtk_text_buffer_insert(buffer, &iter, "Device successfully disconnected\n", -1);
  g_print("Device successfully disconnected\n");
}

static void device_connect_popup(GtkWidget *widget, gpointer data)
{
  GtkApplication *app = GTK_APPLICATION(data);
  GtkTextBuffer *buffer = g_object_get_data(G_OBJECT(widget), "buffer");
  GtkWidget *popup;
  GtkWidget *entry;
  const gchar *device;

  popup = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(popup), "Connect");
  gtk_window_set_default_size(GTK_WINDOW(popup), 200, 200);  // TODO: remove hardcoded values, asap
  gtk_window_set_resizable(GTK_WINDOW(popup), FALSE);

  entry = gtk_entry_new();
  g_signal_connect(entry, "activate", G_CALLBACK(connect_entry), buffer);

  gtk_window_set_child(GTK_WINDOW(popup), entry);
  gtk_window_present(GTK_WINDOW(popup));
}

static void clear_terminal(GtkWidget *widget, gpointer data)
{
  GtkTextBuffer *buffer = g_object_get_data(G_OBJECT(widget), "buffer");
  
  gtk_text_buffer_set_text(buffer, "", -1);
}

static void activate(GtkApplication *app, gpointer user_data)
{
  GtkWidget *window;
  GtkWidget *main_box;
  GtkWidget *terminal;
  GtkWidget *scrolled_window;
  GtkWidget *button_box;
  GtkWidget *button;
  GtkTextBuffer *buffer;

  apply_css();

  // Create main window
  window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Window");
  gtk_window_set_default_size(GTK_WINDOW(window), 800, 400);
  gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

  // Create a horizontal box as the main container
  main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_margin_start(main_box, 10);
  gtk_widget_set_margin_end(main_box, 10);
  gtk_widget_set_margin_top(main_box, 10);
  gtk_widget_set_margin_bottom(main_box, 10);

  // Create a scrolled window for the terminal output
  scrolled_window = gtk_scrolled_window_new();
  gtk_widget_set_hexpand(scrolled_window, TRUE);
  gtk_widget_set_vexpand(scrolled_window, TRUE);
  
  // Create terminal (text view)
  terminal = gtk_text_view_new();
  buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(terminal));
  gtk_text_view_set_editable(GTK_TEXT_VIEW(terminal), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(terminal), FALSE);
  gtk_text_buffer_set_text(buffer, "Terminal Output:\n", -1);
  // TODO: set terminal follow new lines
  
  // Add the terminal to the scrolled window
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), terminal);
  
  // Add the scrolled window to the main box
  gtk_box_append(GTK_BOX(main_box), scrolled_window);

  // Create a vertical box for buttons
  button_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(button_box, 10);
  gtk_widget_set_valign(button_box, GTK_ALIGN_CENTER);
  
  // Button 1: Send data (opens popup)
  button = gtk_button_new_with_label("Send data");
  g_signal_connect(button, "clicked", G_CALLBACK(send_data_popup), app);
  gtk_box_append(GTK_BOX(button_box), button);
  
  // Button 2: Clear terminal 
  button = gtk_button_new_with_label("Clear terminal");
  g_object_set_data(G_OBJECT(button), "buffer", buffer);
  g_signal_connect(button, "clicked", G_CALLBACK(clear_terminal), app);
  gtk_box_append(GTK_BOX(button_box), button);
  
  // Button 3: Connect
  button = gtk_button_new_with_label("Connect");
  g_object_set_data(G_OBJECT(button), "buffer", buffer);
  g_signal_connect(button, "clicked", G_CALLBACK(device_connect_popup), app);
  gtk_box_append(GTK_BOX(button_box), button);

  // Button 4: Disconnect (quit)
  button = gtk_button_new_with_label("Disconnect");
  //g_signal_connect_swapped(button, "clicked", G_CALLBACK(gtk_window_destroy), window);
  g_object_set_data(G_OBJECT(button), "buffer", buffer);
  g_signal_connect(button, "clicked", G_CALLBACK(disconnect_device), app);
  gtk_box_append(GTK_BOX(button_box), button);
  
  // Add the button box to the main box
  gtk_box_append(GTK_BOX(main_box), button_box);
  
  // Set the main box as the child of the window
  gtk_window_set_child(GTK_WINDOW(window), main_box);
  
  // Show the window
  gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv)
{
  GtkApplication *app;
  int status;
  
  app = gtk_application_new("org.gtk.example", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  
  return status;
}
