#include <gtk/gtk.h>

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
  gtk_window_set_child(GTK_WINDOW(popup), label);
  gtk_window_present(GTK_WINDOW(popup));
}

static void print_hi_2(GtkWidget *widget, gpointer data)
{
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER(data);
  GtkTextIter iter;
  
  gtk_text_buffer_get_end_iter(buffer, &iter);
  gtk_text_buffer_insert(buffer, &iter, "Hi 2\n", -1);
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
  
  // Button 2: Clear terminal (prints Hi 2)
  button = gtk_button_new_with_label("Print to terminal");
  g_signal_connect(button, "clicked", G_CALLBACK(print_hi_2), buffer);
  gtk_box_append(GTK_BOX(button_box), button);
  
  // Button 3: Disconnect (quit)
  button = gtk_button_new_with_label("Disconnect");
  g_signal_connect_swapped(button, "clicked", G_CALLBACK(gtk_window_destroy), window);
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
