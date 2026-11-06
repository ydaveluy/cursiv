package blog.archive;

class Snapshot {
    private Date taken;
    private Post[] posts;

    public void setTaken(Date taken) {
        this.taken = taken;
    }

    public Date getTaken() {
        return taken;
    }

    public void setPosts(Post[] posts) {
        this.posts = posts;
    }

    public Post[] getPosts() {
        return posts;
    }
}
