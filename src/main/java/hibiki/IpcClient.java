package hibiki;

import hibiki.pb.commands.Request;
import hibiki.pb.notifications.Notification;
import java.io.*;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.Consumer;
import java.util.logging.Level;
import java.util.logging.Logger;

public class IpcClient {
  private static final Logger LOG = Logger.getLogger(IpcClient.class.getName());
  private static final int IPC_MAGIC = 0x48424B49; // "HBKI"

  private final List<Consumer<Notification>> listeners = new ArrayList<>();
  private ExecutorService executor;
  private DataOutputStream out;

  public void start(InputStream stdOut, InputStream stdErr, OutputStream stdIn) {
    if (stdIn == null || stdOut == null) {
      LOG.warning("Cannot start IPC: null streams");
      return;
    }
    this.out = new DataOutputStream(stdIn);
    this.executor = Executors.newCachedThreadPool();
    executor.submit(() -> readStdout(stdOut));
    if (stdErr != null) {
      executor.submit(() -> readStderr(stdErr));
    }
  }

  public void stop() {
    if (executor != null) {
      executor.shutdownNow();
      executor = null;
    }
    out = null;
  }

  public void addNotificationListener(Consumer<Notification> listener) {
    synchronized (listeners) {
      listeners.add(listener);
    }
  }

  public void removeNotificationListener(Consumer<Notification> listener) {
    synchronized (listeners) {
      listeners.remove(listener);
    }
  }

  public synchronized void sendRequest(Request request) {
    if (out == null) {
      LOG.warning("Backend not ready, request dropped.");
      return;
    }
    try {
      byte[] data = request.toByteArray();
      int size = data.length;
      out.writeInt(Integer.reverseBytes(size));
      out.write(data);
      out.flush();
    } catch (IOException e) {
      LOG.log(Level.SEVERE, "Failed to send request", e);
    }
  }

  private void readStdout(InputStream inputStream) {
    try (DataInputStream in = new DataInputStream(inputStream)) {
      int msgCount = 0;
      while (true) {
        int magic = Integer.reverseBytes(in.readInt());
        if (magic != IPC_MAGIC) {
          LOG.warning(
              "Invalid magic: 0x"
                  + Integer.toHexString(magic)
                  + " at msg#"
                  + msgCount
                  + ", resyncing...");
          int resyncCount = 0;
          int buf = Integer.reverseBytes(magic);
          while (resyncCount < 10000) {
            int b = in.readByte() & 0xFF;
            buf = (buf << 8) | b;
            if (buf == 0x494B4248) {
              LOG.info("Resynced after " + resyncCount + " bytes");
              break;
            }
            resyncCount++;
          }
          if (resyncCount >= 10000) {
            LOG.severe("Could not resync after 10000 bytes, skipping...");
            continue;
          }
        }

        int size = Integer.reverseBytes(in.readInt());
        msgCount++;

        if (size < 0 || size > 10 * 1024 * 1024) {
          LOG.severe("Invalid size: " + size + " at msg#" + msgCount + ", skipping...");
          continue;
        }

        byte[] data = new byte[size];
        in.readFully(data);

        Notification notification = Notification.parseFrom(data);
        handleNotification(notification);
      }
    } catch (IOException e) {
      LOG.info("Backend stdout closed: " + e.getMessage());
    }
  }

  private void readStderr(InputStream inputStream) {
    try (BufferedReader reader = new BufferedReader(new InputStreamReader(inputStream))) {
      String line;
      while ((line = reader.readLine()) != null) {
        System.err.println(line);
      }
    } catch (IOException e) {
      LOG.log(Level.WARNING, "Backend stderr reader failed", e);
    }
  }

  void handleNotification(Notification notification) {
    synchronized (listeners) {
      for (Consumer<Notification> listener : listeners) {
        try {
          listener.accept(notification);
        } catch (Exception e) {
          LOG.log(Level.WARNING, "Notification listener error", e);
        }
      }
    }
  }
}
