import java.io.File;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;
import java.util.List;
import java.util.Locale;
import java.util.Random;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

public class PZLiveAgent {
    private static String session = System.currentTimeMillis() + "-" + new Random().nextInt(1000000);
    private static ScheduledExecutorService svc;
    private static boolean loggedBind;
    private static boolean loggedError;

    public static void premain(String args) { start(args); }
    public static void agentmain(String args) { start(args); }

    private static synchronized void start(String args) {
        if (svc != null) return;
        System.out.println("[PZLiveAgent] loaded");
        svc = Executors.newSingleThreadScheduledExecutor();
        svc.scheduleWithFixedDelay(() -> tick(args), 0, 250, TimeUnit.MILLISECONDS); // site polls at 4 Hz
    }

    private static Object call(Object obj, String name, Class<?>[] sig, Object[] argv) throws Exception {
        Method m = obj instanceof Class
            ? ((Class<?>) obj).getMethod(name, sig)
            : obj.getClass().getMethod(name, sig);
        return m.invoke(obj, argv);
    }

    private static Object first(Object obj, String[] names) {
        for (String n : names) {
            try {
                return call(obj, n, new Class[0], new Object[0]);
            } catch (Exception ignored) { }
        }
        return null;
    }

    private static String esc(String s) {
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '\\') b.append("\\\\");
            else if (c == '"') b.append("\\\"");
            else if (c < 0x20 || c == 0x7f) b.append(' ');
            else b.append(c);
        }
        return b.toString();
    }

    @SuppressWarnings("unchecked")
    private static void tick(String args) {
        try {
            Class<?> isoPlayer = Class.forName("zombie.characters.IsoPlayer");
            Object listObj = first(isoPlayer, new String[]{"getPlayers"});
            if (!(listObj instanceof List)) {
                try {
                    Class<?> gc = Class.forName("zombie.network.GameClient");
                    listObj = first(gc, new String[]{"getPlayers"});
                } catch (Exception ignored) { }
            }
            if (!(listObj instanceof List)) return;
            List<Object> players = (List<Object>) listObj;
            if (players.isEmpty()) return;
            Object p = players.get(0);
            if (p == null) return;

            float x = ((Number) call(p, "getX", new Class[0], new Object[0])).floatValue();
            float y = ((Number) call(p, "getY", new Class[0], new Object[0])).floatValue();
            float zf = ((Number) call(p, "getZ", new Class[0], new Object[0])).floatValue();
            int z = Math.round(zf);
            boolean dead = Boolean.TRUE.equals(call(p, "isDead", new Class[0], new Object[0]));

            String name = "Survivor";
            try {
                Object desc = call(p, "getDescriptor", new Class[0], new Object[0]);
                if (desc != null) name = String.valueOf(call(desc, "getForename", new Class[0], new Object[0]));
            } catch (Exception ignored) { }
            if (name == null || name.isEmpty() || "null".equals(name)) {
                try { name = String.valueOf(call(p, "getUsername", new Class[0], new Object[0])); }
                catch (Exception ignored) { name = "Survivor"; }
            }

            String dirPart = "";
            try {
                Object fx = call(p, "getForwardDirectionX", new Class[0], new Object[0]);
                Object fy = call(p, "getForwardDirectionY", new Class[0], new Object[0]);
                dirPart = String.format(Locale.ROOT, ",\"fx\":%.2f,\"fy\":%.2f",
                    ((Number) fx).doubleValue(), ((Number) fy).doubleValue());
            } catch (Exception e1) {
                try {
                    Object fwd = call(p, "getForwardDirection", new Class[0], new Object[0]);
                    double fxd, fyd;
                    try {
                        fxd = ((Number) call(fwd, "getX", new Class[0], new Object[0])).doubleValue();
                        fyd = ((Number) call(fwd, "getY", new Class[0], new Object[0])).doubleValue();
                    } catch (Exception e2) {
                        fxd = fwd.getClass().getField("x").getFloat(fwd);
                        fyd = fwd.getClass().getField("y").getFloat(fwd);
                    }
                    dirPart = String.format(Locale.ROOT, ",\"fx\":%.2f,\"fy\":%.2f", fxd, fyd);
                } catch (Exception ignored) { }
            }

            StringBuilder others = new StringBuilder();
            int count = 0;
            for (int i = 1; i < players.size() && count < 64; i++) { // site shows max 64
                Object o = players.get(i);
                if (o == null) continue;
                try {
                    float ox = ((Number) call(o, "getX", new Class[0], new Object[0])).floatValue();
                    float oy = ((Number) call(o, "getY", new Class[0], new Object[0])).floatValue();
                    float ozf = ((Number) call(o, "getZ", new Class[0], new Object[0])).floatValue();
                    boolean oDead = Boolean.TRUE.equals(call(o, "isDead", new Class[0], new Object[0]));
                    String on = "Survivor";
                    try { on = String.valueOf(call(o, "getUsername", new Class[0], new Object[0])); }
                    catch (Exception ignored) { }
                    if (others.length() > 0) others.append(",");
                    others.append(String.format(Locale.ROOT, "{\"name\":\"%s\",\"x\":%.1f,\"y\":%.1f,\"z\":%d%s}",
                        esc(on), ox, oy, Math.round(ozf), oDead ? ",\"dead\":true" : ""));
                    count++;
                } catch (Exception ignored) { }
            }

            String base = args != null && !args.isEmpty() ? args : defaultBase();
            if (File.separatorChar == '\\' && base.startsWith("/")) base = "Z:" + base;
            File out = new File(base, "Lua/pzm_live/live.txt");
            out.getParentFile().mkdirs();
            long now = System.currentTimeMillis();
            String json = String.format(Locale.ROOT,
                "{\"v\":1,\"session\":\"%s\",\"t\":%d,\"name\":\"%s\",\"x\":%.1f,\"y\":%.1f,\"z\":%d%s%s%s}",
                esc(session), now, esc(name), x, y, z, dirPart,
                dead ? ",\"dead\":true" : "",
                others.length() > 0 ? ",\"others\":[" + others + "]" : "");
            Path target = out.toPath();
            Path temp = target.resolveSibling(target.getFileName() + ".tmp");
            Files.writeString(temp, json, StandardCharsets.UTF_8,
                StandardOpenOption.CREATE, StandardOpenOption.TRUNCATE_EXISTING, StandardOpenOption.WRITE);
            try {
                Files.move(temp, target, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
            } catch (AtomicMoveNotSupportedException ignored) {
                Files.move(temp, target, StandardCopyOption.REPLACE_EXISTING);
            }
            if (!loggedBind) {
                loggedBind = true;
                System.out.println("[PZLiveAgent] bound IsoPlayer API, session " + session);
            }
        } catch (Exception e) {
            if (!loggedError) {
                loggedError = true;
                System.err.println("[PZLiveAgent] update failed");
                e.printStackTrace();
            }
        }
    }

    private static String defaultBase() {
        String home = System.getProperty("user.home");
        String[] cands = {
            home + "/Zomboid",
            System.getenv("USERPROFILE") + "/Zomboid",
        };
        for (String c : cands) {
            if (c != null && new File(c).isDirectory()) return c;
        }
        return home + "/Zomboid";
    }
}
