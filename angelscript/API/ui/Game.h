/**
 * Game
 */
class Game
{
public:
	/* object properties */

	/* object behaviors */

	/* object methods */
	const DemoInfo& get_demo() const;
	void startLocalSound() const;
	void startBackgroundTrack(String&in, String&in, bool arg2 = true) const;
	void stopBackgroundTrack() const;
	String@ get_name() const;
	String@ configString(int) const;
	String@ cs(int) const;
	int get_clientState() const;
	int get_serverState() const;
	void exec(const String&in) const;
	void execAppend(const String&in) const;
	void execInsert(const String&in) const;
	void print(const String&in) const;
	void dprint(const String&in) const;
};

