#include <termios.h>
#include <fcntl.h>
#include <signal.h>

#include <gtk/gtk.h>

int32_t SERIAL_ID = -1;
gboolean CONNECTED = FALSE;
gboolean TRANSMITTING = FALSE;
// TODO: add timer that waits for read

#define BG_COLOR "#323232"
#define TEXT_COLOR "#f0f0f0" 
#define BUTTON_BG "#424242"
#define BUTTON_HOVER "#5a5a5a"
#define TERMINAL_BG "#252525"
#define TERMINAL_TEXT "#e0e0e0"
#define ACCENT_COLOR "#ff8800"

#define TIMEOUT 3
#define APP_HEIGHT 400
#define APP_WIDTH 800
#define POPUP_WIDTH 200
#define POPUP_HEIGHT 200

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

static void app_shutdown(GApplication *app, gpointer user_data)
{
  if (SERIAL_ID >= 0 && CONNECTED) {
    g_print("Application shutting down, closing serial connection...\n");
    close(SERIAL_ID);
    SERIAL_ID = -1;
    CONNECTED = FALSE;
  }
}

void signal_handler(int signal)
{
  if (SERIAL_ID >= 0 && CONNECTED) {
    printf("Caught signal %d, cleaning up serial connection...\n", signal);
    close(SERIAL_ID);
  }
  exit(0);
}

static void setup_signals()
{
  signal(SIGINT, signal_handler);   // Ctrl+C
  signal(SIGTERM, signal_handler);  // Termination request
  signal(SIGHUP, signal_handler);   // Terminal closed
  signal(SIGQUIT, signal_handler);  // (Ctrl+\)
}

static void on_buffer_changed(GtkTextBuffer *buffer, gpointer user_data)
{
  GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
  GtkTextIter iter;
  
  // Scroll
  gtk_text_buffer_get_end_iter(buffer, &iter);
  gtk_text_view_scroll_to_iter(text_view, &iter, 0.0, TRUE, 0.0, 1.0);
}

static void print_data(GtkTextBuffer *buffer, gpointer msg_buffer)
{
  GtkTextIter iter;
  if (buffer) {
    gtk_text_buffer_get_end_iter(buffer, &iter);
    gtk_text_buffer_insert(buffer, &iter, msg_buffer, -1);
  }
  g_print(msg_buffer);
}

static void send_command(GtkWidget *widget, gpointer data)
{
  typedef struct {
    const char *command;
    GtkTextBuffer *buffer;
  } CommandData;
  
  CommandData *cmd_data = (CommandData *)data;
  const char *command = cmd_data->command;
  GtkTextBuffer *buffer = cmd_data->buffer;
  
  GtkRoot *popup = gtk_widget_get_root(widget);
  gtk_window_destroy(GTK_WINDOW(popup));

  int32_t i, read_size = 0;
  char read_buffer[100];
  char msg_buffer[256];

  // Send data
  write(SERIAL_ID, command, strlen(command));
  tcdrain(SERIAL_ID);
  
  g_snprintf(msg_buffer, sizeof(msg_buffer), "Sending: %s", command);
  print_data(buffer, msg_buffer);
  
  tcflush(SERIAL_ID, TCIOFLUSH);
  // Read incoming data
  for(i = 0; i < TIMEOUT; i++) {
    read_size = read(SERIAL_ID, read_buffer, sizeof(read_buffer) - 1);
    
    g_snprintf(msg_buffer, sizeof(msg_buffer), "read size: %d\n", read_size);
    print_data(buffer, msg_buffer);

    if (read_size > 1) {
      read_buffer[read_size] = '\0';
      if(!strcmp("Done\n\n", read_buffer) || !strcmp("Done", read_buffer)) {
        print_data(buffer, (gpointer)"Got positive response\n");
        break;
      }
      else if (strcmp("Done\n", read_buffer) && i == (TIMEOUT - 1)) {
        print_data(buffer, (gpointer)"Didn't get response\n");
      }
    }
  }
  
  g_free(cmd_data);
}

static void send_data_popup(GtkWidget *widget, gpointer data)
{
  GtkApplication *app = GTK_APPLICATION(data);
  GtkWidget *popup;
  GtkWidget *label;
  GtkWidget *button_box;
  GtkWidget *button;
  
  // Terminal buffer from the button
  GtkTextBuffer *buffer = g_object_get_data(G_OBJECT(widget), "buffer");
 
  popup = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(popup), "Popup");
  gtk_window_set_default_size(GTK_WINDOW(popup), POPUP_WIDTH, POPUP_HEIGHT);
  gtk_window_set_resizable(GTK_WINDOW(popup), FALSE);
  
  button_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(button_box, 10);
  gtk_widget_set_margin_end(button_box, 10);
  gtk_widget_set_valign(button_box, GTK_ALIGN_CENTER);
  
  // Temp struct
  typedef struct {
    const char *command;
    GtkTextBuffer *buffer;
  } CommandData;
  
  // Button 1: Turn on
  CommandData *on_data = g_new(CommandData, 1);
  on_data->command = "Turn on\n";
  on_data->buffer = buffer;
  button = gtk_button_new_with_label("Turn on");
  g_signal_connect(button, "clicked", G_CALLBACK(send_command), on_data);
  gtk_box_append(GTK_BOX(button_box), button);

  // Button 2: Turn off
  CommandData *off_data = g_new(CommandData, 1);
  off_data->command = "Turn off\n";
  off_data->buffer = buffer;
  button = gtk_button_new_with_label("Turn off");
  g_signal_connect(button, "clicked", G_CALLBACK(send_command), off_data);
  gtk_box_append(GTK_BOX(button_box), button);

  gtk_window_set_child(GTK_WINDOW(popup), button_box);
  gtk_window_present(GTK_WINDOW(popup));
}

static void connect_entry(GtkEntry *entry, gpointer *data)
{
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER(data);
  const gchar *device_name = gtk_editable_get_text(GTK_EDITABLE(entry));
  char device_path[256] = "/dev/";
  char msg_buffer[385];
  
  strncat(device_path, device_name, sizeof(device_path) - strlen("/dev/") - 1);
  
  GtkRoot *popup = gtk_widget_get_root(GTK_WIDGET(entry));
  if(GTK_WINDOW(popup)) {
    SERIAL_ID = open(device_path, O_RDWR);
    if(SERIAL_ID < 0) {
      g_snprintf(msg_buffer, sizeof(msg_buffer), "ERROR: Failed to open serial port: %s\n", device_path);
      print_data(buffer, msg_buffer);
      goto destroy_conn_popup;
    }
    if (terminal_init(SERIAL_ID)) {
      print_data(buffer, (gpointer)"ERROR: Failed to setup terminal connection!\n");
      close(SERIAL_ID);
      goto destroy_conn_popup;
    }
    else {
      CONNECTED = TRUE;
      g_snprintf(msg_buffer, sizeof(msg_buffer), "Successfully connected to device: %s\n", device_path);
      print_data(buffer, msg_buffer);
    }
    
  destroy_conn_popup:
    gtk_window_destroy(GTK_WINDOW(popup));
  }
}

static void connect_device_popup(GtkWidget *widget, gpointer data)
{
  GtkApplication *app = GTK_APPLICATION(data);
  GtkTextBuffer *buffer = g_object_get_data(G_OBJECT(widget), "buffer");
  GtkWidget *popup;
  GtkWidget *entry;
  const gchar *device;
  
  if(!CONNECTED) {
    popup = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(popup), "Connect");
    gtk_window_set_default_size(GTK_WINDOW(popup), POPUP_WIDTH, POPUP_HEIGHT);
    gtk_window_set_resizable(GTK_WINDOW(popup), FALSE);

    entry = gtk_entry_new();
    g_signal_connect(entry, "activate", G_CALLBACK(connect_entry), buffer);

    gtk_window_set_child(GTK_WINDOW(popup), entry);
    gtk_window_present(GTK_WINDOW(popup));
  }
  else {
    print_data(buffer, "Device is already connected\n");
  }
}

static void disconnect_device(GtkWidget *widget, gpointer data)
{
  GtkTextBuffer *buffer = g_object_get_data(G_OBJECT(widget), "buffer");

  if(CONNECTED) {
    close(SERIAL_ID);
    CONNECTED = FALSE;
    print_data(buffer, "Device successfully disconnected\n");
  }
  else {
    print_data(buffer, "Device is not connected\n");
  }
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
  gtk_window_set_default_size(GTK_WINDOW(window), APP_WIDTH, APP_HEIGHT);
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
  
  // Create terminal
  terminal = gtk_text_view_new();
  buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(terminal));
  gtk_text_view_set_editable(GTK_TEXT_VIEW(terminal), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(terminal), FALSE);
  gtk_text_buffer_set_text(buffer, "Terminal Output:\n", -1);
  g_signal_connect(buffer, "changed", G_CALLBACK(on_buffer_changed), terminal);
  
  // Add the terminal to the scrolled window
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), terminal);
  
  // Add the scrolled window to the main box
  gtk_box_append(GTK_BOX(main_box), scrolled_window);

  // Button box
  button_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(button_box, 10);
  gtk_widget_set_valign(button_box, GTK_ALIGN_CENTER);
  
  // Button 1: Send data
  button = gtk_button_new_with_label("Send data");
  g_object_set_data(G_OBJECT(button), "buffer", buffer);
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
  g_signal_connect(button, "clicked", G_CALLBACK(connect_device_popup), app);
  gtk_box_append(GTK_BOX(button_box), button);

  // Button 4: Disconnect
  button = gtk_button_new_with_label("Disconnect");
  g_object_set_data(G_OBJECT(button), "buffer", buffer);
  g_signal_connect(button, "clicked", G_CALLBACK(disconnect_device), app);
  gtk_box_append(GTK_BOX(button_box), button);

  gtk_box_append(GTK_BOX(main_box), button_box);
  gtk_window_set_child(GTK_WINDOW(window), main_box);
  gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv)
{
  GtkApplication *app;
  int status;
  
  setup_signals();

  app = gtk_application_new("org.gtk.example", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  g_signal_connect(app, "shutdown", G_CALLBACK(app_shutdown), NULL);
  status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  
  return status;
}


// TODO: allow only one popup window to be open -> gboolean OPEN_POPUP = FALSE;
// TODO: wait for command to be over before sending other command (handle on a server side?) -> gboolean TRANSMITTING = FALSE;
// TODO: add statement that checks if device is connected before sending data
