public final class Hello {
    private static String identityField(String key) throws Exception {
        java.io.BufferedReader reader = new java.io.BufferedReader(
                new java.io.FileReader("/proc/self/status"));
        try {
            String line;
            while ((line = reader.readLine()) != null) {
                if (line.startsWith(key + ":")) return line.substring(key.length() + 1).trim();
            }
        } finally {
            reader.close();
        }
        return "missing";
    }

    public static void main(String[] args) {
        int[] values = new int[1000];
        for (int i=0;i<values.length;i++) values[i]=i;
        int total=0; for(int v:values) total+=v;
        if (total!=499500) throw new AssertionError("arithmetic");
        System.out.println("HELLO_ART_RESULT="+(2026+1));
        System.out.println("HELLO_ART_SUM="+total);
        System.out.println("HELLO_ART_VM="+System.getProperty("java.vm.name"));
        try {
            System.out.println("HELLO_ART_UID="+identityField("Uid"));
            System.out.println("HELLO_ART_GID="+identityField("Gid"));
            if (args.length > 0 && "zygote".equals(args[0])) Thread.sleep(1500);
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }
}
