#include <gtk/gtk.h>

static void send_data_popup(GtkWindow *widget, gpointer data)
{
  GtkWindow *popup;
  GtkWidget *grid;
  GtkWidget *button;
  GtkWidget *label;

  popup = GTK_WINDOW(gtk_application_window_new(GTK_APPLICATION(data)));
  gtk_window_set_title(popup, "Popup");
  gtk_window_set_default_size(popup, 200, 200);
  gtk_window_set_resizable(GTK_WINDOW(popup), FALSE);

  label = gtk_label_new("Hmmmmm");
  gtk_window_set_child(popup, label);
  gtk_window_present(popup);
}

static void print_hi_2(GtkWidget *widget, gpointer data)
{
  g_print("Hi 2\n");
}

static void activate(GtkApplication *app, gpointer user_data)
{
  GtkWidget *window;
  GtkWidget *grid;
  GtkWidget *terminal_output;
  GtkWidget *button;

  window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Window");
  gtk_window_set_default_size(GTK_WINDOW(window), 800, 400);
  gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

  /* Main grid */
  grid = gtk_grid_new();
  gtk_widget_set_hexpand(grid, TRUE);
  gtk_widget_set_hexpand(grid, TRUE);
  /*
   * (a, b, c, d)
   *  a -> column
   *  b -> row
   *  c -> width (in columns)
   *  d -> height (in rows)
   */

  /* Terminal output - left */
  terminal_output = gtk_grid_new();
  GtkWidget *t_label = gtk_label_new("Terminal");
  gtk_grid_attach(GTK_GRID(terminal_output), t_label, 0, 0, 1, 3);


  /* Button box */
  GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10); // 10 = spacing

  /* Buttons */
  button = gtk_button_new_with_label("Send data");
  g_signal_connect(button, "clicked", G_CALLBACK(send_data_popup), app);
  gtk_box_append(GTK_BOX(button_box), button);

  button = gtk_button_new_with_label("Clear terminal");
  g_signal_connect(button, "clicked", G_CALLBACK (print_hi_2), NULL);
  gtk_box_append(GTK_BOX(button_box), button);

  button = gtk_button_new_with_label("Disconnect");
  g_signal_connect_swapped(button, "clicked", G_CALLBACK(gtk_window_destroy), window);
  gtk_box_append(GTK_BOX(button_box), button);
  
  gtk_grid_attach(GTK_GRID(grid), terminal_output, 1, 0, 1, 3);
  gtk_grid_attach(GTK_GRID(grid), button_box, 3, 0, 1, 3);
  /**/
  gtk_window_set_child(GTK_WINDOW(window), grid);
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
